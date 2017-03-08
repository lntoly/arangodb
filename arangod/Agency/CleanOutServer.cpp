////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Kaveh Vahedipour
////////////////////////////////////////////////////////////////////////////////

#include "CleanOutServer.h"

#include "Agency/AgentInterface.h"
#include "Agency/Job.h"
#include "Agency/MoveShard.h"

using namespace arangodb::consensus;

CleanOutServer::CleanOutServer(Node const& snapshot, AgentInterface* agent,
                               std::string const& jobId,
                               std::string const& creator,
                               std::string const& server)
    : Job(NOTFOUND, snapshot, agent, jobId, creator), _server(id(server)) {}

CleanOutServer::CleanOutServer(Node const& snapshot, AgentInterface* agent,
                               JOB_STATUS status, std::string const& jobId)
    : Job(status, snapshot, agent, jobId) {
  // Get job details from agency:
  try {
    std::string path = pos[status] + _jobId + "/";
    _server = _snapshot(path + "server").getString();
    _creator = _snapshot(path + "creator").getString();
  } catch (std::exception const& e) {
    std::stringstream err;
    err << "Failed to find job " << _jobId << " in agency: " << e.what();
    LOG_TOPIC(ERR, Logger::AGENCY) << err.str();
    finish("DBServers/" + _server, false, err.str());
    _status = FAILED;
  }
}

CleanOutServer::~CleanOutServer() {}

void CleanOutServer::run() {
  runHelper("DBServers/" + _server);
}

JOB_STATUS CleanOutServer::status() {
  if (_status != PENDING) {
    return _status;
  }

  Node::Children const todos = _snapshot(toDoPrefix).children();
  Node::Children const pends = _snapshot(pendingPrefix).children();
  size_t found = 0;

  for (auto const& subJob : todos) {
    if (!subJob.first.compare(0, _jobId.size() + 1, _jobId + "-")) {
      found++;
    }
  }
  for (auto const& subJob : pends) {
    if (!subJob.first.compare(0, _jobId.size() + 1, _jobId + "-")) {
      found++;
    }
  }

  // FIXME: implement timeout here?

  if (found == 0) {
    // Put server in /Target/CleanedServers:
    Builder reportTrx;
    {
      VPackArrayBuilder guard(&reportTrx);
      {
        VPackObjectBuilder guard3(&reportTrx);
        reportTrx.add(VPackValue(agencyPrefix + "/Target/CleanedServers"));
        {
          VPackObjectBuilder guard4(&reportTrx);
          reportTrx.add("op", VPackValue("push"));
          reportTrx.add("new", VPackValue(_server));
        }
      }
    }
    // Transact to agency
    write_ret_t res = transact(_agent, reportTrx);

    if (res.accepted && res.indices.size() == 1 && res.indices[0] != 0) {
      LOG_TOPIC(DEBUG, Logger::AGENCY) << "Have reported " << _server
                                      << " in /Target/CleanedServers";
    } else {
      LOG_TOPIC(ERR, Logger::AGENCY) << "Failed to report " << _server
                                     << " in /Target/CleanedServers";
    }

    if (finish("DBServers/" + _server)) {
      return FINISHED;
    }
  }

  return _status;
}

// Only through shrink cluster
bool CleanOutServer::create(std::shared_ptr<VPackBuilder> b) {

  LOG_TOPIC(DEBUG, Logger::AGENCY)
      << "Todo: Clean out server " + _server + " for shrinkage";

  std::string path = agencyPrefix + toDoPrefix + _jobId;

  _jb = std::make_shared<Builder>();
  _jb->openArray();
  _jb->openObject();
  _jb->add(path, VPackValue(VPackValueType::Object));
  _jb->add("type", VPackValue("cleanOutServer"));
  _jb->add("server", VPackValue(_server));
  _jb->add("jobId", VPackValue(_jobId));
  _jb->add("creator", VPackValue(_creator));
  _jb->add("timeCreated",
           VPackValue(timepointToString(std::chrono::system_clock::now())));
  _jb->close();
  _jb->close();
  _jb->close();

  write_ret_t res = transact(_agent, *_jb);

  if (res.accepted && res.indices.size() == 1 && res.indices[0]) {
    return true;
  }

  LOG_TOPIC(INFO, Logger::AGENCY) << "Failed to insert job " + _jobId;
  return false;
}

bool CleanOutServer::start() {
  // Copy todo to pending
  Builder todo, pending;

  // Get todo entry
  todo.openArray();
  if (_jb == nullptr) {
    try {
      _snapshot(toDoPrefix + _jobId).toBuilder(todo);
    } catch (std::exception const&) {
      LOG_TOPIC(INFO, Logger::AGENCY) << "Failed to get key " + toDoPrefix +
                                             _jobId + " from agency snapshot";
      return false;
    }
  } else {
    todo.add(_jb->slice()[0].valueAt(0));
  }
  todo.close();

  // Enter pending, remove todo, block toserver
  pending.openArray();

  // --- Add pending
  pending.openObject();
  pending.add(agencyPrefix + pendingPrefix + _jobId,
              VPackValue(VPackValueType::Object));
  pending.add("timeStarted",
              VPackValue(timepointToString(std::chrono::system_clock::now())));
  for (auto const& obj : VPackObjectIterator(todo.slice()[0])) {
    pending.add(obj.key.copyString(), obj.value);
  }
  pending.close();

  // --- Delete todo
  pending.add(agencyPrefix + toDoPrefix + _jobId,
              VPackValue(VPackValueType::Object));
  pending.add("op", VPackValue("delete"));
  pending.close();

  // --- Block toServer
  pending.add(agencyPrefix + blockedServersPrefix + _server,
              VPackValue(VPackValueType::Object));
  pending.add("jobId", VPackValue(_jobId));
  pending.close();

  // --- Announce in Sync that server is cleaning out
  /*  pending.add(agencyPrefix + serverStatePrefix + _server,
                VPackValue(VPackValueType::Object));
    pending.add("cleaning", VPackValue(true));
    pending.close();*/

  pending.close();

  // Preconditions
  // --- Check that toServer not blocked
  pending.openObject();
  pending.add(agencyPrefix + blockedServersPrefix + _server,
              VPackValue(VPackValueType::Object));
  pending.add("oldEmpty", VPackValue(true));
  pending.close();

  pending.close();
  pending.close();

  // Transact to agency
  write_ret_t res = transact(_agent, pending);

  if (res.accepted && res.indices.size() == 1 && res.indices[0]) {
    LOG_TOPIC(DEBUG, Logger::AGENCY) << "Pending: Clean out server " + _server;

    // Check if we can get things done in the first place
    if (!checkFeasibility()) {
      finish("DBServers/" + _server, false);
      return false;
    }

    // Schedule shard relocations
    if (!scheduleMoveShards()) {
      finish("DBServers/" + _server, false, "Could not schedule MoveShard.");
      return false;
    }

    return true;
  }

  LOG_TOPIC(INFO, Logger::AGENCY)
      << "Precondition failed for starting job " + _jobId;

  return false;
}

bool CleanOutServer::scheduleMoveShards() {

  std::vector<std::string> servers = availableServers(_snapshot);

  // Minimum 1 DB server must remain
  if (servers.size() == 1) {
    LOG_TOPIC(ERR, Logger::AGENCY) << "DB server " << _server
                                   << " is the last standing db server.";
    return false;
  }

  Node::Children const& databases = _snapshot("/Plan/Collections").children();
  size_t sub = 0;

  for (auto const& database : databases) {
    
    // Find shardsLike dependencies
    for (auto const& collptr : database.second->children()) {
      
      auto const& collection = *(collptr.second);
      
      try { // distributeShardsLike entry means we only follow
        if (collection("distributeShardsLike").slice().copyString() != "") {
          continue;
        }
      } catch (...) {}

      for (auto const& shard : collection("shards").children()) {
        
        int found = -1;
        VPackArrayIterator dbsit(shard.second->slice());

        // Only shards, which are affected
        int count = 0;
        for (auto const& dbserver : dbsit) {
          if (dbserver.copyString() == _server) {
            found = count;
            break;
          }
          count++;
        }
        if (found == -1) {
          continue;
        }

        // Only destinations, which are not already holding this shard
        for (auto const& dbserver : dbsit) {
          servers.erase(
            std::remove(servers.begin(), servers.end(), dbserver.copyString()),
            servers.end());
        }

        // Among those a random destination
        std::string toServer;
        if (servers.empty()) {
          LOG_TOPIC(DEBUG, Logger::AGENCY)
            << "No servers remain as target for MoveShard";
          return false;
        }

        // FIXME: use RandomGenerator here
        try {
          toServer = servers.at(rand() % servers.size());
        } catch (...) {
          LOG_TOPIC(ERR, Logger::AGENCY)
            << "Range error picking destination for shard " + shard.first;
        }

        // FIXME: check conditions: server healthy, not cleaned, not failed,
        // FIXME: is this doen in checkFeasibility?
        // FIXME: check shards being locked
        // FIXME: check servers being locked
        // FIXME: is it necessary to create all MoveShard jobs in one 
        // FIXME: transaction? Do we need to check the precondition that
        // FIXME: the server is not blocked?
        // FIXME: What if some MoveShard job does not work?

        // Schedule move
        MoveShard(_snapshot, _agent, _jobId + "-" + std::to_string(sub++),
                  _jobId, database.first, collptr.first,
                  shard.first, _server, toServer, found == 0).run();
        
      }
    }
  }

  return true;
  
}

bool CleanOutServer::checkFeasibility() {
  // Server exists
  if (_snapshot.exists("/Plan/DBServers/" + _server).size() != 3) {
    LOG_TOPIC(ERR, Logger::AGENCY)
      << "No db server with id " << _server << " in plan.";
    return false;
  }

  // Server has not been cleaned already
  for (auto const& srv :
         VPackArrayIterator(_snapshot("/Target/CleanedServers").slice())) {
    if (srv.copyString() == _server) {
      LOG_TOPIC(ERR, Logger::AGENCY)
        << _server << " has been cleaned out already!";
      return false;
    }
  }

  // Server has not failed already
  for (auto const& srv :
         VPackObjectIterator(_snapshot("/Target/FailedServers").slice())) {
    if (srv.key.copyString() == _server) {
      LOG_TOPIC(ERR, Logger::AGENCY) << _server << " has failed!";
      return false;
    }
  }
  
  if (_snapshot.exists(serverStatePrefix + _server + "/cleaning").size() == 4) {
    LOG_TOPIC(ERR, Logger::AGENCY)
      << _server << " has been cleaned out already!";
    return false;
  }

  std::vector<std::string> availServers;

  // Get servers from plan
  Node::Children const& dbservers = _snapshot("/Plan/DBServers").children();
  for (auto const& srv : dbservers) {
    availServers.push_back(srv.first);
  }

  // Remove cleaned from ist
  if (_snapshot.exists("/Target/CleanedServers").size() == 2) {
    for (auto const& srv :
           VPackArrayIterator(_snapshot("/Target/CleanedServers").slice())) {
      availServers.erase(
        std::remove(
          availServers.begin(), availServers.end(), srv.copyString()),
        availServers.end());
    }
  }

  // Minimum 1 DB server must remain
  if (availServers.size() == 1) {
    LOG_TOPIC(ERR, Logger::AGENCY)
      << "DB server " << _server << " is the last standing db server.";
    return false;
  }

  // Remaning after clean out
  uint64_t numRemaining = availServers.size() - 1;

  // Find conflictings collections
  uint64_t maxReplFact = 1;
  std::vector<std::string> tooLargeCollections;
  std::vector<uint64_t> tooLargeFactors;
  Node::Children const& databases = _snapshot("/Plan/Collections").children();
  for (auto const& database : databases) {
    for (auto const& collptr : database.second->children()) {
      try {
        uint64_t replFact = (*collptr.second)("replicationFactor").getUInt();
        if (replFact > numRemaining) {
          tooLargeCollections.push_back(collptr.first);
          tooLargeFactors.push_back(replFact);
        }
        if (replFact > maxReplFact) {
          maxReplFact = replFact;
        }
      } catch (...) {
      }
    }
  }

  // Report if problems exist
  if (maxReplFact > numRemaining) {
    std::stringstream collections;
    std::stringstream factors;

    for (auto const collection : tooLargeCollections) {
      collections << collection << " ";
    }
    for (auto const factor : tooLargeFactors) {
      factors << std::to_string(factor) << " ";
    }

    LOG_TOPIC(ERR, Logger::AGENCY)
        << "Cannot accomodate shards " << collections.str()
        << "with replication factors " << factors.str()
        << "after cleaning out server " << _server;
    return false;
  }

  return true;
}

void CleanOutServer::abort() {
  // TO BE IMPLEMENTED
}

