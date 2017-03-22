////////////////////////////////////////////////////////////////////////////////
/// @brief test case for AddFollower job
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2017 ArangoDB GmbH, Cologne, Germany
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
/// @author Copyright 2017, ArangoDB GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////
#include "catch.hpp"
#include "fakeit.hpp"

#include "Agency/AddFollower.h"
#include "Agency/FailedLeader.h"
#include "Agency/MoveShard.h"
#include "Agency/AgentInterface.h"
#include "Agency/Node.h"
#include "lib/Basics/StringUtils.h"
#include "lib/Random/RandomGenerator.h"
#include <iostream>
#include <velocypack/Parser.h>
#include <velocypack/Slice.h>
#include <velocypack/velocypack-aliases.h>

using namespace arangodb;
using namespace arangodb::basics;
using namespace arangodb::consensus;
using namespace fakeit;

namespace arangodb {
namespace tests {
namespace add_follower_test {

const std::string PREFIX = "arango";
const std::string DATABASE = "database";
const std::string COLLECTION = "collection";
const std::string SHARD = "shard";
const std::string SHARD_LEADER = "leader";
const std::string SHARD_FOLLOWER1 = "follower1";
const std::string SHARD_FOLLOWER2 = "follower2";
const std::string FREE_SERVER = "free";
const std::string FREE_SERVER2 = "free2";

const char *agency =
#include "AddFollowerTest.json"
  ;
const char *todo =
#include "AddFollowerTestToDo.json"
  ;


Node createNodeFromBuilder(VPackBuilder const& builder) {

  VPackBuilder opBuilder;
  { VPackObjectBuilder a(&opBuilder);
    opBuilder.add("new", builder.slice()); }
  
  Node node("");
  node.handle<SET>(opBuilder.slice());
  return node;

}

Builder createBuilder(char const* c) {

  Options options;
  options.checkAttributeUniqueness = true;
  VPackParser parser(&options);
  parser.parse(c);
  
  Builder builder;
  builder.add(parser.steal()->slice());

  return builder;
  
}

Node createNode(char const* c) {
  return createNodeFromBuilder(createBuilder(c));
}

Node createRootNode() {
  return createNode(agency);
}

typedef std::function<std::unique_ptr<VPackBuilder>(
  VPackSlice const&, std::string const&)> TestStructType;

TEST_CASE("AddFollower", "[agency][supervision]") {
  
  auto baseStructure = createRootNode();
  arangodb::RandomGenerator::initialize(arangodb::RandomGenerator::RandomType::MERSENNE);
    
  VPackBuilder builder;
  baseStructure.toBuilder(builder);

  write_ret_t fakeWriteResult {true, "", std::vector<bool> {true}, std::vector<index_t> {1}};
  trans_ret_t fakeTransResult {true, "", 1, 0, std::make_shared<Builder>()};
  
  SECTION("creating a job should create a job in todo") {
    Mock<AgentInterface> mockAgent;

    std::string jobId = "1";
    When(Method(mockAgent, write)).AlwaysDo([&](query_t const& q) -> write_ret_t {
        INFO(q->slice().toJson());
        auto expectedJobKey = "/arango/Target/ToDo/" + jobId;
        REQUIRE(q->slice().typeName() == "array" );
        REQUIRE(q->slice().length() == 1);
        REQUIRE(q->slice()[0].typeName() == "array");
        REQUIRE(q->slice()[0].length() == 1); // we always simply override! no preconditions...
        REQUIRE(q->slice()[0][0].typeName() == "object");
        REQUIRE(q->slice()[0][0].length() == 1); // should ONLY do an entry in todo
        REQUIRE(q->slice()[0][0].get(expectedJobKey).typeName() == "object");

        auto job = q->slice()[0][0].get(expectedJobKey);
        REQUIRE(job.get("creator").typeName() == "string");
        REQUIRE(job.get("type").typeName() == "string");
        CHECK(job.get("type").copyString() == "addFollower");
        REQUIRE(job.get("database").typeName() == "string");
        CHECK(job.get("database").copyString() == DATABASE);
        REQUIRE(job.get("collection").typeName() == "string");
        CHECK(job.get("collection").copyString() == COLLECTION);
        REQUIRE(job.get("shard").typeName() == "string");
        CHECK(job.get("shard").copyString() == SHARD);
        CHECK(job.get("jobId").typeName() == "string");
        CHECK(job.get("timeCreated").typeName() == "string");
        
        return fakeWriteResult;
      });

    When(Method(mockAgent, waitFor)).AlwaysReturn(AgentInterface::raft_commit_t::OK);
    AgentInterface &agent = mockAgent.get();

    auto addFollower = AddFollower(
      baseStructure, &agent, jobId, "unittest", DATABASE, COLLECTION, SHARD);
    
    addFollower.create();
    
  }

  SECTION("<collection> still exists, if missing, job is finished, move to "
          "Target/Finished") {

    std::string jobId = "1";

    TestStructType createTestStructure = [&](
      VPackSlice const& s, std::string const& path) {

      std::unique_ptr<VPackBuilder> builder;
      if (path == "/arango/Plan/Collections/" + DATABASE + "/" + COLLECTION) {
        return builder;
      }

      builder = std::make_unique<VPackBuilder>();
      
      if (s.isObject()) {

        VPackObjectBuilder b(builder.get());

        for (auto const& it: VPackObjectIterator(s)) {
          auto childBuilder = createTestStructure(it.value, path + "/" + it.key.copyString());
          if (childBuilder) {
            builder->add(it.key.copyString(), childBuilder->slice());
          }
        }
        
        if (path == "/arango/Target/ToDo") {
          builder->add("1", createBuilder(todo).slice());
        }
        
        builder->close();
        
      } else {
        builder->add(s);
      }
      
      return builder;
    };
    
    auto builder = createTestStructure(baseStructure.toBuilder().slice(), "");
    REQUIRE(builder);
    Node agency = createNodeFromBuilder(*builder);
    
    Mock<AgentInterface> mockAgent;
    When(Method(mockAgent, write)).AlwaysDo([&](query_t const& q) -> write_ret_t {
        INFO(q->slice().toJson());
        REQUIRE(q->slice().typeName() == "array" );
        REQUIRE(q->slice().length() == 1);
        REQUIRE(q->slice()[0].typeName() == "array");
        // we always simply override! no preconditions...
        REQUIRE(q->slice()[0].length() == 1); 
        REQUIRE(q->slice()[0][0].typeName() == "object");
        
        auto writes = q->slice()[0][0];
        REQUIRE(writes.get("/arango/Target/ToDo/1").typeName() == "object");
        REQUIRE(writes.get("/arango/Target/ToDo/1").get("op").typeName() == "string");
        CHECK(writes.get("/arango/Target/ToDo/1").get("op").copyString() == "delete");
        CHECK(writes.get("/arango/Target/Finished/1").typeName() == "object");
        return fakeWriteResult;
      });
    
    When(Method(mockAgent, waitFor)).AlwaysReturn(AgentInterface::raft_commit_t::OK);
    AgentInterface &agent = mockAgent.get();

    auto addFollower =
      AddFollower(agency("arango"), &agent, JOB_STATUS::TODO, jobId);
    addFollower.start();
    
  }
  
  
  SECTION("if <collection> has a non-empty distributeShardsLike attribute, the "
          "job immediately fails and is moved to Target/Failed") {

    std::string jobId = "1";

    TestStructType createTestStructure = [&](
      Slice const& s, std::string const& path) {

      std::unique_ptr<Builder> builder;
      builder.reset(new Builder());
      if (s.isObject()) {
        
        VPackObjectBuilder b(builder.get());

        for (auto const& it: VPackObjectIterator(s)) {
          auto childBuilder =
            createTestStructure(it.value, path + "/" + it.key.copyString());
          if (childBuilder) {
            builder->add(it.key.copyString(), childBuilder->slice());
          }
        }
        
        if (path == "/arango/Plan/Collections/" + DATABASE + "/" + COLLECTION) {
          builder->add("distributeShardsLike", VPackValue("PENG"));
        }
        
        if (path == "/arango/Target/ToDo") {
          builder->add("1", createBuilder(todo).slice());
        }

      } else {
        builder->add(s);
      }
      return builder;

    };
    
    auto builder = createTestStructure(baseStructure.toBuilder().slice(), "");
    REQUIRE(builder);
    Node agency = createNodeFromBuilder(*builder);

    Mock<AgentInterface> mockAgent;
    When(Method(mockAgent, write)).AlwaysDo([&](query_t const& q) -> write_ret_t {
        INFO(q->slice().toJson());
        REQUIRE(q->slice().typeName() == "array" );
        REQUIRE(q->slice().length() == 1);
        REQUIRE(q->slice()[0].typeName() == "array");
        REQUIRE(q->slice()[0].length() == 1); // we always simply override! no preconditions...
        REQUIRE(q->slice()[0][0].typeName() == "object");

        auto writes = q->slice()[0][0];
        REQUIRE(writes.get("/arango/Target/ToDo/1").typeName() == "object");
        REQUIRE(writes.get("/arango/Target/ToDo/1").get("op").typeName() == "string");
        CHECK(writes.get("/arango/Target/ToDo/1").get("op").copyString() == "delete");
        CHECK(writes.get("/arango/Target/Failed/1").typeName() == "object");
        return fakeWriteResult;
      });
    When(Method(mockAgent, waitFor)).AlwaysReturn(AgentInterface::raft_commit_t::OK);
    AgentInterface &agent = mockAgent.get();
    auto addFollower =
      AddFollower(agency("arango"), &agent, JOB_STATUS::TODO, jobId);
    addFollower.start();
    
  }
  
  SECTION("condition (*) still holds for the mentioned collections, if not, job "
          "is finished, move to Target/Finished") {

    std::string jobId = "1";
    
    TestStructType createTestStructure = [&](
      Slice const& s, std::string const& path) {
      
      std::unique_ptr<Builder> builder;
      builder.reset(new Builder());
      if (s.isObject()) {
        
        VPackObjectBuilder b(builder.get());

        for (auto const& it: VPackObjectIterator(s)) {
          auto childBuilder =
            createTestStructure(it.value, path + "/" + it.key.copyString());
          if (childBuilder) {
            builder->add(it.key.copyString(), childBuilder->slice());
          }
        }
        
        if (path == "/arango/Target/ToDo") {
          builder->add("1", createBuilder(todo).slice());
        }
        
      } else {
        
        if (path == "/arango/Plan/Collections/" + DATABASE + "/" +
            COLLECTION + "/shards/" + SHARD) {
          VPackArrayBuilder a(builder.get());
          for (auto const& serv : VPackArrayIterator(s)) {
            builder->add(serv);
          }
          builder->add(VPackValue(SHARD_FOLLOWER2));
        } else {
          builder->add(s);
        }
        
      }
      return builder;
      
    };
    
    auto builder = createTestStructure(baseStructure.toBuilder().slice(), "");
    REQUIRE(builder);
    std::cout << builder->toJson();
    Node agency = createNodeFromBuilder(*builder);
    
    Mock<AgentInterface> mockAgent;
    When(Method(mockAgent, write)).AlwaysDo([&](query_t const& q) -> write_ret_t {
        INFO(q->slice().toJson());
        REQUIRE(q->slice().typeName() == "array" );
        REQUIRE(q->slice().length() == 1);
        REQUIRE(q->slice()[0].typeName() == "array");
        REQUIRE(q->slice()[0].length() == 1); // we always simply override! no preconditions...
        REQUIRE(q->slice()[0][0].typeName() == "object");

        auto writes = q->slice()[0][0];
        REQUIRE(writes.get("/arango/Target/ToDo/1").typeName() == "object");
        REQUIRE(writes.get("/arango/Target/ToDo/1").get("op").typeName() == "string");
        CHECK(writes.get("/arango/Target/ToDo/1").get("op").copyString() == "delete");
        CHECK(writes.get("/arango/Target/Finished/1").get("collection").copyString() == COLLECTION);
        CHECK(writes.get("/arango/Target/Pending/1").get("op").copyString() == "delete");
        CHECK(writes.get("/arango/Target/Failed/1").typeName() == "none");
        return fakeWriteResult;
      });
    When(Method(mockAgent, waitFor)).AlwaysReturn(AgentInterface::raft_commit_t::OK);
    AgentInterface &agent = mockAgent.get();
    auto addFollower =
      AddFollower(agency("arango"), &agent, JOB_STATUS::TODO, jobId);
    addFollower.start();
    
  }
  
  SECTION("if there is no job under Supervision/Shards/<shard> (if so, do "
          "nothing, leave job in ToDo)") {
    
  }
  
  SECTION("we can find one (or more, if needed) which have status 'GOOD', and "
          "have `Supervision/DBServers/ empty and are not currently in the list "
          "of servers of the shard, if not, wait") {
    
  }

  SECTION("this job is immediately performed in a single transaction and then "
          "moved to Target/Finished") {
    
  }

  SECTION("As long as the job is still in Target/ToDo it can safely be aborted "
          "and moved to Target/Finished") {
  }
  
};

}}}
