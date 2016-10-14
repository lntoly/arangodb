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

#include "Constituent.h"

#include <chrono>
#include <iomanip>
#include <thread>

#include <velocypack/Iterator.h>
#include <velocypack/velocypack-aliases.h>

#include "Agency/Agent.h"
#include "Aql/Query.h"
#include "Aql/QueryRegistry.h"
#include "Basics/ConditionLocker.h"
#include "Cluster/ClusterComm.h"
#include "Logger/Logger.h"
#include "Random/RandomGenerator.h"
#include "Utils/OperationOptions.h"
#include "Utils/OperationResult.h"
#include "Utils/SingleCollectionTransaction.h"
#include "Utils/StandaloneTransactionContext.h"
#include "VocBase/vocbase.h"

using namespace arangodb::consensus;
using namespace arangodb::rest;
using namespace arangodb::velocypack;
using namespace arangodb;

//  (std::numeric_limits<std::string>::max)();

/// Raft role names for display purposes
const std::vector<std::string> roleStr({"Follower", "Candidate", "Leader"});

/// Configure with agent's configuration
void Constituent::configure(Agent* agent) {
  _agent = agent;
  TRI_ASSERT(_agent != nullptr);

  if (size() == 1) {
    _role = LEADER;
    LOG_TOPIC(INFO, Logger::AGENCY) << "Set _role to LEADER in term "  << _term;
  } else {
    _id = _agent->config().id();
  }
}

// Default ctor
Constituent::Constituent()
    : Thread("Constituent"),
      _vocbase(nullptr),
      _queryRegistry(nullptr),
      _term(0),
      _cast(false),
      _leaderID(NO_LEADER),
      _lastHeartbeatSeen(0.0),
      _role(FOLLOWER),
      _agent(nullptr),
      _votedFor(NO_LEADER) {}

/// Shutdown if not already
Constituent::~Constituent() { shutdown(); }

/// Wait for sync
bool Constituent::waitForSync() const { return _agent->config().waitForSync(); }

/// Random sleep times in election process
duration_t Constituent::sleepFor(double min_t, double max_t) {
  int32_t left = static_cast<int32_t>(1000.0 * min_t),
          right = static_cast<int32_t>(1000.0 * max_t);
  return duration_t(static_cast<long>(RandomGenerator::interval(left, right)));
}

/// Get my term
term_t Constituent::term() const {
  MUTEX_LOCKER(guard, _castLock);
  return _term;
}

/// Update my term
void Constituent::term(term_t t) {
  MUTEX_LOCKER(guard, _castLock);
  termNoLock(t);
}

void Constituent::termNoLock(term_t t) {
  // Only call this when you have the _castLock
  term_t tmp = _term;
  _term = t;

  if (tmp != t) {
    LOG_TOPIC(DEBUG, Logger::AGENCY) << _id << ": " << roleStr[_role]
                                     << " term " << t;

    _cast = false;
    _votedFor = "";
    Builder body;
    body.add(VPackValue(VPackValueType::Object));
    std::ostringstream i_str;
    i_str << std::setw(20) << std::setfill('0') << t;
    body.add("_key", Value(i_str.str()));
    body.add("term", Value(t));
    body.add("voted_for", Value(_votedFor));
    body.close();

    TRI_ASSERT(_vocbase != nullptr);
    auto transactionContext =
        std::make_shared<StandaloneTransactionContext>(_vocbase);
    SingleCollectionTransaction trx(transactionContext, "election",
                                    TRI_TRANSACTION_WRITE);

    int res = trx.begin();

    if (res != TRI_ERROR_NO_ERROR) {
      THROW_ARANGO_EXCEPTION(res);
    }

    OperationOptions options;
    options.waitForSync = false;
    options.silent = true;

    OperationResult result = trx.insert("election", body.slice(), options);
    trx.finish(result.code);
  }
}

/// My role
role_t Constituent::role() const {
  MUTEX_LOCKER(guard, _castLock);
  return _role;
}

/// Become follower in term
void Constituent::follow(term_t t) {
  MUTEX_LOCKER(guard, _castLock);
  followNoLock(t);
}

void Constituent::followNoLock(term_t t) {
  if (_role != FOLLOWER) {
    LOG_TOPIC(DEBUG, Logger::AGENCY)
        << _id << ": Converting to follower in term " << t;
  }

  _term = t;
  _role = FOLLOWER;
  LOG_TOPIC(INFO, Logger::AGENCY) << "Set _role to FOLLOWER in term " << _term;
  {
    CONDITION_LOCKER(guard, _cv);
    _cv.signal();
  }
}

/// Become leader
void Constituent::lead(term_t term,
                       std::map<std::string, bool> const& votes) {
  {
    MUTEX_LOCKER(guard, _castLock);

    if (term < _term) {
      followNoLock(_term);
      return;
    }

    if (_role == LEADER) {
      return;
    }

    _role = LEADER;
    LOG_TOPIC(INFO, Logger::AGENCY) << "Set _role to LEADER in term " << _term;
    if (_leaderID != _id) {
      LOG_TOPIC(INFO, Logger::AGENCY) << "Set _leaderID to " << _id;
      _leaderID = _id;
    }
  }

  if (!votes.empty()) {
    std::stringstream ss;
    ss << _id << ": Converted to leader in term " << _term << " with votes (";
    for (auto const& vote : votes) {
      ss << vote.second;
    }
    ss << ")";
    LOG_TOPIC(DEBUG, Logger::AGENCY) << ss.str();
  }

  _agent->lead();  // We need to rebuild spear_head and read_db;
}

/// Become candidate
void Constituent::candidate() {
  MUTEX_LOCKER(guard, _castLock);

  if (_leaderID != NO_LEADER) {
    _leaderID = NO_LEADER;
    LOG_TOPIC(INFO, Logger::AGENCY) << "Set _leaderID to NO_LEADER";
  }

  if (_role != CANDIDATE) {
    _role = CANDIDATE;
    LOG_TOPIC(INFO, Logger::AGENCY) << "Set _role to CANDIDATE in term "
      << _term;
  }
}

/// Leading?
bool Constituent::leading() const {
  MUTEX_LOCKER(guard, _castLock);
  return _role == LEADER;
}

/// Following?
bool Constituent::following() const {
  MUTEX_LOCKER(guard, _castLock);
  return _role == FOLLOWER;
}

/// Running as candidate?
bool Constituent::running() const {
  MUTEX_LOCKER(guard, _castLock);
  return _role == CANDIDATE;
}

/// Get current leader's id
std::string Constituent::leaderID() const { return _leaderID; }

/// Agency size
size_t Constituent::size() const { return _agent->config().size(); }

/// Get endpoint to an id
std::string Constituent::endpoint(std::string id) const {
  return _agent->config().poolAt(id);
}

/// @brief Check leader
bool Constituent::checkLeader(term_t term, std::string id, index_t prevLogIndex,
                              term_t prevLogTerm) {
  TRI_ASSERT(_vocbase);
  {
    MUTEX_LOCKER(guard, _castLock);
    if (term >= _term) {
      _lastHeartbeatSeen = TRI_microtime();
      if (_leaderID != id) {
        LOG_TOPIC(INFO, Logger::AGENCY) << "Set _leaderID to " << id
          << " in term " << _term;
        _leaderID = id;
        TRI_ASSERT(_leaderID != _id);
      }
      return true;
    }
    return false;
  }
}

/// @brief Vote
bool Constituent::vote(term_t term, std::string id, index_t prevLogIndex,
                       term_t prevLogTerm) {

  TRI_ASSERT(_vocbase);
  
  term_t t = 0;
  std::string lid;

  MUTEX_LOCKER(guard, _castLock);

  if (term > t) {
    this->termNoLock(term);
    _cast = true;
    _votedFor = id;
    if (_role != FOLLOWER) {
      followNoLock(_term);
    }
    return true;
  }

  if (t == term) {
    if (!_cast) {
      _votedFor = id;
      _cast = true;
      if (_role != FOLLOWER) {
        followNoLock(_term);
      }
      return true;
    }
    if (_votedFor == id) {
      if (_role != FOLLOWER) {
        followNoLock(_term);
      }
      return true;
    }
  }

  return false;
}

/// @brief Call to election
void Constituent::callElection() {
  std::map<std::string, bool> votes;
  std::vector<std::string> active =
      _agent->config().active();  // Get copy of active

  votes[_id] = true;  // vote for myself
  LOG_TOPIC(INFO, Logger::AGENCY) << "Set _leaderID to NO_LEADER"
    << " in term " << _term;
  _leaderID = NO_LEADER;
  
  term_t savedTerm;
  {
    MUTEX_LOCKER(locker, _castLock);
    this->termNoLock(_term + 1);  // raise my term
    _cast = true;
    _votedFor = _id;
    savedTerm = _term;
  }

  std::string body;
  std::map<std::string, OperationID> operationIDs;
  std::stringstream path;

  path << "/_api/agency_priv/requestVote?term=" << savedTerm
       << "&candidateId=" << _id << "&prevLogIndex=" << _agent->lastLog().index
       << "&prevLogTerm=" << _agent->lastLog().term;

  double minPing = _agent->config().minPing();

  double respTimeout = 0.9 * minPing;
  double initTimeout = 0.5 * minPing;

  // Ask everyone for their vote
  for (auto const& i : active) {
    if (i != _id) {
      auto headerFields =
          std::make_unique<std::unordered_map<std::string, std::string>>();
      operationIDs[i] = ClusterComm::instance()->asyncRequest(
        "1", 1, _agent->config().poolAt(i),
        rest::RequestType::GET, path.str(),
        std::make_shared<std::string>(body), headerFields,
        nullptr, respTimeout, true, initTimeout);
    }
  }

  // Wait randomized timeout
  std::this_thread::sleep_for(sleepFor(initTimeout, respTimeout));

  // Collect votes
  for (const auto& i : active) {
    if (i != _id) {
      ClusterCommResult res =
          arangodb::ClusterComm::instance()->enquire(operationIDs[i]);
      if (res.status == CL_COMM_SENT) {  // Request successfully sent
        res = ClusterComm::instance()->wait("1", 1, operationIDs[i], "1");
        std::shared_ptr<Builder> body = res.result->getBodyVelocyPack();
        if (body->isEmpty()) {  // body empty
          continue;
        } else {
          if (body->slice().isObject()) {  // body
            VPackSlice slc = body->slice();
            if (slc.hasKey("term") && slc.hasKey("voteGranted")) {  // OK
              term_t t = slc.get("term").getUInt();
              if (t > _term) {  // follow?
                follow(t);
                break;
              }
              votes[i] = slc.get("voteGranted").getBool();  // Get vote
            }
          }
        }
      } else {  // Request failed
        votes[i] = false;
      }
    }
  }

  // Count votes
  size_t yea = 0;
  for (auto const& i : votes) {
    if (i.second) {
      ++yea;
    }
  }

  {
    MUTEX_LOCKER(locker, _castLock);
    if (savedTerm != _term) {
      followNoLock(_term);
      return;
    }
  }

  // Evaluate election results
  if (yea > size() / 2) {
    lead(savedTerm, votes);
  } else {
    follow(_term);
  }
}

void Constituent::update(std::string const& leaderID, term_t t) {
  MUTEX_LOCKER(guard, _castLock);
  _term = t;
  if (_leaderID != leaderID) {
    LOG_TOPIC(INFO, Logger::AGENCY)
      << "Constituent::update: setting _leaderID to " << leaderID
      << " in term " << _term;
    _leaderID = leaderID;
  }
}

/// Start clean shutdown
void Constituent::beginShutdown() {
  Thread::beginShutdown();

  CONDITION_LOCKER(guard, _cv);
  guard.broadcast();
}

/// Start operation
bool Constituent::start(TRI_vocbase_t* vocbase,
                        aql::QueryRegistry* queryRegistry) {
  TRI_ASSERT(vocbase != nullptr);
  _vocbase = vocbase;
  _queryRegistry = queryRegistry;

  return Thread::start();
}

/// Get persisted information and run election process
void Constituent::run() {
  _id = _agent->config().id();

  TRI_ASSERT(_vocbase != nullptr);
  auto bindVars = std::make_shared<VPackBuilder>();
  bindVars->openObject();
  bindVars->close();

  // Most recent vote
  std::string const aql("FOR l IN election SORT l._key DESC LIMIT 1 RETURN l");
  arangodb::aql::Query query(false, _vocbase, aql.c_str(), aql.size(), bindVars,
                             nullptr, arangodb::aql::PART_MAIN);

  auto queryResult = query.execute(_queryRegistry);
  if (queryResult.code != TRI_ERROR_NO_ERROR) {
    THROW_ARANGO_EXCEPTION_MESSAGE(queryResult.code, queryResult.details);
  }

  VPackSlice result = queryResult.result->slice();

  if (result.isArray()) {
    for (auto const& i : VPackArrayIterator(result)) {
      try {
        MUTEX_LOCKER(locker, _castLock);
        _term = i.get("term").getUInt();
        _votedFor = i.get("voted_for").copyString();
      } catch (std::exception const&) {
        LOG_TOPIC(ERR, Logger::AGENCY)
            << "Persisted election entries corrupt! Defaulting term,vote (0,0)";
      }
    }
  }

  std::vector<std::string> act = _agent->config().active();
  while (
    !this->isStopping() // Obvious
    && (!_agent->ready()
        || find(act.begin(), act.end(), _id) == act.end())) { // Active agent 
    CONDITION_LOCKER(guardv, _cv);
    _cv.wait(50000);
    act = _agent->config().active();
  }

  if (size() == 1) {
    _leaderID = _agent->config().id();
    LOG_TOPIC(INFO, Logger::AGENCY) << "Set _leaderID to " << _leaderID
      << " in term " << _term;
  } else {
    while (!this->isStopping()) {
      if (_role == FOLLOWER) {
        
        int32_t left = static_cast<int32_t>(1000000.0 *
                                            _agent->config().minPing()),
          right = static_cast<int32_t>(1000000.0 *
                                       _agent->config().maxPing());
        long rand_wait =
          static_cast<long>(RandomGenerator::interval(left, right));
        
        LOG_TOPIC(DEBUG, Logger::AGENCY) << "Random timeout: " << rand_wait;

        double now = TRI_microtime();
        if (_lastHeartbeatSeen != 0) {   // in the beginning, pure random
          rand_wait -= (now - _lastHeartbeatSeen);
        }
        if (rand_wait > 0.0) {
          CONDITION_LOCKER(guardv, _cv);
          _cv.wait(rand_wait);
        }

        now = TRI_microtime();
        if (now - _lastHeartbeatSeen > rand_wait) {
          candidate();
        }
      } else if (_role == CANDIDATE) {
        callElection();  // Run for office
      } else {
        int32_t left =
          static_cast<int32_t>(100000.0 * _agent->config().minPing());
        long rand_wait = static_cast<long>(left);
        {
          CONDITION_LOCKER(guardv, _cv);
          _cv.wait(rand_wait);
        }
      }
    }
  }
}
