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
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#include "FunctionDefinitions.h"
#include "Aql/AstNode.h"
#include "Aql/Function.h"
#include "Cluster/ServerState.h"

using namespace arangodb::aql;

/// @brief determines if code is executed in cluster or not
static ExecutionCondition const NotInCluster =
    [] { return !arangodb::ServerState::instance()->isRunningInCluster(); };

/// @brief determines if code is executed on coordinator or not
static ExecutionCondition const NotInCoordinator = [] {
  return !arangodb::ServerState::instance()->isRunningInCluster() ||
         !arangodb::ServerState::instance()->isCoordinator();
};

/// @brief internal functions used in execution
std::unordered_map<int,
                   std::string const> const FunctionDefinitions::InternalFunctionNames{
    {static_cast<int>(NODE_TYPE_OPERATOR_UNARY_PLUS), "UNARY_PLUS"},
    {static_cast<int>(NODE_TYPE_OPERATOR_UNARY_MINUS), "UNARY_MINUS"},
    {static_cast<int>(NODE_TYPE_OPERATOR_UNARY_NOT), "LOGICAL_NOT"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_EQ), "RELATIONAL_EQUAL"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_NE), "RELATIONAL_UNEQUAL"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_GT), "RELATIONAL_GREATER"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_GE), "RELATIONAL_GREATEREQUAL"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_LT), "RELATIONAL_LESS"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_LE), "RELATIONAL_LESSEQUAL"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_IN), "RELATIONAL_IN"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_NIN), "RELATIONAL_NOT_IN"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_PLUS), "ARITHMETIC_PLUS"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_MINUS), "ARITHMETIC_MINUS"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_TIMES), "ARITHMETIC_TIMES"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_DIV), "ARITHMETIC_DIVIDE"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_MOD), "ARITHMETIC_MODULUS"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_AND), "LOGICAL_AND"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_OR), "LOGICAL_OR"},
    {static_cast<int>(NODE_TYPE_OPERATOR_TERNARY), "TERNARY_OPERATOR"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_ARRAY_EQ), "RELATIONAL_ARRAY_EQUAL"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_ARRAY_NE), "RELATIONAL_ARRAY_UNEQUAL"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_ARRAY_GT), "RELATIONAL_ARRAY_GREATER"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_ARRAY_GE), "RELATIONAL_ARRAY_GREATEREQUAL"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_ARRAY_LT), "RELATIONAL_ARRAY_LESS"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_ARRAY_LE), "RELATIONAL_ARRAY_LESSEQUAL"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_ARRAY_IN), "RELATIONAL_ARRAY_IN"},
    {static_cast<int>(NODE_TYPE_OPERATOR_BINARY_ARRAY_NIN), "RELATIONAL_ARRAY_NOT_IN"}};

/// @brief user-accessible functions
std::unordered_map<std::string, Function const> const FunctionDefinitions::FunctionNames{
    // meanings of the symbols in the function arguments list
    // ------------------------------------------------------
    //
    // . = argument of any type (except collection)
    // c = collection name, will be converted into list with documents
    // h = collection name, will be converted into string
    // z = null
    // b = bool
    // n = number
    // s = string
    // p = primitive
    // l = array
    // a = object / document
    // r = regex (a string with a special format). note: the regex type is
    // mutually exclusive with all other types

    // type check functions
    {"IS_NULL", Function("IS_NULL", "AQL_IS_NULL", ".", true, true, false, true,
                         true, &Functions::IsNull)},
    {"IS_BOOL", Function("IS_BOOL", "AQL_IS_BOOL", ".", true, true, false, true,
                         true, &Functions::IsBool)},
    {"IS_NUMBER", Function("IS_NUMBER", "AQL_IS_NUMBER", ".", true, true, false,
                           true, true, &Functions::IsNumber)},
    {"IS_STRING", Function("IS_STRING", "AQL_IS_STRING", ".", true, true, false,
                           true, true, &Functions::IsString)},
    {"IS_ARRAY", Function("IS_ARRAY", "AQL_IS_ARRAY", ".", true, true, false,
                          true, true, &Functions::IsArray)},
    // IS_LIST is an alias for IS_ARRAY
    {"IS_LIST", Function("IS_LIST", "AQL_IS_LIST", ".", true, true, false, true,
                         true, &Functions::IsArray)},
    {"IS_OBJECT", Function("IS_OBJECT", "AQL_IS_OBJECT", ".", true, true, false,
                           true, true, &Functions::IsObject)},
    // IS_DOCUMENT is an alias for IS_OBJECT
    {"IS_DOCUMENT", Function("IS_DOCUMENT", "AQL_IS_DOCUMENT", ".", true, true,
                             false, true, true, &Functions::IsObject)},
    {"IS_DATESTRING", Function("IS_DATESTRING", "AQL_IS_DATESTRING", ".", true,
                               true, false, true, true)},
    {"TYPENAME", Function("TYPENAME", "AQL_TYPENAME", ".", true,
                               true, false, true, true, &Functions::Typename)},

    // type cast functions
    {"TO_NUMBER", Function("TO_NUMBER", "AQL_TO_NUMBER", ".", true, true, false,
                           true, true, &Functions::ToNumber)},
    {"TO_STRING", Function("TO_STRING", "AQL_TO_STRING", ".", true, true, false,
                           true, true, &Functions::ToString)},
    {"TO_BOOL", Function("TO_BOOL", "AQL_TO_BOOL", ".", true, true, false, true,
                         true, &Functions::ToBool)},
    {"TO_ARRAY", Function("TO_ARRAY", "AQL_TO_ARRAY", ".", true, true, false,
                          true, true, &Functions::ToArray)},
    // TO_LIST is an alias for TO_ARRAY
    {"TO_LIST", Function("TO_LIST", "AQL_TO_LIST", ".", true, true, false, true,
                         true, &Functions::ToArray)},

    // string functions
    {"CONCAT", Function("CONCAT", "AQL_CONCAT", "szl|+", true, true, false,
                        true, true, &Functions::Concat)},
    {"CONCAT_SEPARATOR", Function("CONCAT_SEPARATOR", "AQL_CONCAT_SEPARATOR",
                                  "s,szl|+", true, true, false, true, true)},
    {"CHAR_LENGTH", Function("CHAR_LENGTH", "AQL_CHAR_LENGTH", "s", true, true,
                             false, true, true)},
    {"LOWER",
     Function("LOWER", "AQL_LOWER", "s", true, true, false, true, true)},
    {"UPPER",
     Function("UPPER", "AQL_UPPER", "s", true, true, false, true, true)},
    {"SUBSTRING", Function("SUBSTRING", "AQL_SUBSTRING", "s,n|n", true, true,
                           false, true, true)},
    {"CONTAINS", Function("CONTAINS", "AQL_CONTAINS", "s,s|b", true, true,
                          false, true, true, &Functions::Contains)},
    {"LIKE", Function("LIKE", "AQL_LIKE", "s,r|b", true, true, false, true,
                      true, &Functions::Like)},
    {"LEFT",
     Function("LEFT", "AQL_LEFT", "s,n", true, true, false, true, true)},
    {"RIGHT",
     Function("RIGHT", "AQL_RIGHT", "s,n", true, true, false, true, true)},
    {"TRIM",
     Function("TRIM", "AQL_TRIM", "s|ns", true, true, false, true, true)},
    {"LTRIM",
     Function("LTRIM", "AQL_LTRIM", "s|s", true, true, false, true, true)},
    {"RTRIM",
     Function("RTRIM", "AQL_RTRIM", "s|s", true, true, false, true, true)},
    {"FIND_FIRST", Function("FIND_FIRST", "AQL_FIND_FIRST", "s,s|zn,zn", true,
                            true, false, true, true)},
    {"FIND_LAST", Function("FIND_LAST", "AQL_FIND_LAST", "s,s|zn,zn", true,
                           true, false, true, true)},
    {"SPLIT",
     Function("SPLIT", "AQL_SPLIT", "s|sl,n", true, true, false, true, true)},
    {"SUBSTITUTE", Function("SUBSTITUTE", "AQL_SUBSTITUTE", "s,las|lsn,n", true,
                            true, false, true, true)},
    {"MD5", Function("MD5", "AQL_MD5", "s", true, true, false, true, true,
                     &Functions::Md5)},
    {"SHA1", Function("SHA1", "AQL_SHA1", "s", true, true, false, true, true,
                      &Functions::Sha1)},
    {"HASH", Function("HASH", "AQL_HASH", ".", true, true, false, true, true,
                      &Functions::Hash)},
    {"RANDOM_TOKEN", Function("RANDOM_TOKEN", "AQL_RANDOM_TOKEN", "n", false,
                              false, true, true, true, &Functions::RandomToken)},

    // numeric functions
    {"FLOOR", Function("FLOOR", "AQL_FLOOR", "n", true, true, false, true, true,
                       &Functions::Floor)},
    {"CEIL", Function("CEIL", "AQL_CEIL", "n", true, true, false, true, true,
                      &Functions::Ceil)},
    {"ROUND", Function("ROUND", "AQL_ROUND", "n", true, true, false, true, true,
                       &Functions::Round)},
    {"ABS", Function("ABS", "AQL_ABS", "n", true, true, false, true, true,
                     &Functions::Abs)},
    {"RAND", Function("RAND", "AQL_RAND", "", false, false, false, true, true,
                      &Functions::Rand)},
    {"SQRT", Function("SQRT", "AQL_SQRT", "n", true, true, false, true, true,
                      &Functions::Sqrt)},
    {"POW", Function("POW", "AQL_POW", "n,n", true, true, false, true, true,
                     &Functions::Pow)},
    {"LOG", Function("LOG", "AQL_LOG", "n", true, true, false, true, true,
                     &Functions::Log)},
    {"LOG2", Function("LOG2", "AQL_LOG2", "n", true, true, false, true, true,
                      &Functions::Log2)},
    {"LOG10", Function("LOG10", "AQL_LOG10", "n", true, true, false, true, true,
                       &Functions::Log10)},
    {"EXP", Function("EXP", "AQL_EXP", "n", true, true, false, true, true,
                     &Functions::Exp)},
    {"EXP2", Function("EXP2", "AQL_EXP2", "n", true, true, false, true, true,
                      &Functions::Exp2)},
    {"SIN", Function("SIN", "AQL_SIN", "n", true, true, false, true, true,
                     &Functions::Sin)},
    {"COS", Function("COS", "AQL_COS", "n", true, true, false, true, true,
                     &Functions::Cos)},
    {"TAN", Function("TAN", "AQL_TAN", "n", true, true, false, true, true,
                     &Functions::Tan)},
    {"ASIN", Function("ASIN", "AQL_ASIN", "n", true, true, false, true, true,
                      &Functions::Asin)},
    {"ACOS", Function("ACOS", "AQL_ACOS", "n", true, true, false, true, true,
                      &Functions::Acos)},
    {"ATAN", Function("ATAN", "AQL_ATAN", "n", true, true, false, true, true,
                      &Functions::Atan)},
    {"RADIANS", Function("RADIANS", "AQL_RADIANS", "n", true, true, false, true, true,
                      &Functions::Radians)},
    {"DEGREES", Function("DEGREES", "AQL_DEGREES", "n", true, true, false, true, true,
                      &Functions::Degrees)},

    // list functions
    {"RANGE", Function("RANGE", "AQL_RANGE", "n,n|n", true, true, false, true,
                       true, &Functions::Range)},
    {"UNION", Function("UNION", "AQL_UNION", "l,l|+", true, true, false, true,
                       true, &Functions::Union)},
    {"UNION_DISTINCT",
     Function("UNION_DISTINCT", "AQL_UNION_DISTINCT", "l,l|+", true, true,
              false, true, true, &Functions::UnionDistinct)},
    {"MINUS", Function("MINUS", "AQL_MINUS", "l,l|+", true, true, false, true,
                       true, &Functions::Minus)},
    {"INTERSECTION",
     Function("INTERSECTION", "AQL_INTERSECTION", "l,l|+", true, true, false,
              true, true, &Functions::Intersection)},
    {"FLATTEN", Function("FLATTEN", "AQL_FLATTEN", "l|n", true, true, false,
                         true, true, &Functions::Flatten)},
    {"LENGTH", Function("LENGTH", "AQL_LENGTH", "las", true, true, false, true,
                        true, &Functions::Length)},
    {"COUNT", Function("COUNT", "AQL_LENGTH", "las", true, true, false, true,
                       true, &Functions::Length)},  // alias for LENGTH()
    {"MIN", Function("MIN", "AQL_MIN", "l", true, true, false, true, true,
                     &Functions::Min)},
    {"MAX", Function("MAX", "AQL_MAX", "l", true, true, false, true, true,
                     &Functions::Max)},
    {"SUM", Function("SUM", "AQL_SUM", "l", true, true, false, true, true,
                     &Functions::Sum)},
    {"MEDIAN", Function("MEDIAN", "AQL_MEDIAN", "l", true, true, false, true,
                        true, &Functions::Median)},
    {"PERCENTILE", Function("PERCENTILE", "AQL_PERCENTILE", "l,n|s", true, true,
                            false, true, true, &Functions::Percentile)},
    {"AVERAGE", Function("AVERAGE", "AQL_AVERAGE", "l", true, true, false, true,
                         true, &Functions::Average)},
    {"AVG", Function("AVG", "AQL_AVERAGE", "l", true, true, false, true, true,
                     &Functions::Average)},  // alias for AVERAGE()
    {"VARIANCE_SAMPLE",
     Function("VARIANCE_SAMPLE", "AQL_VARIANCE_SAMPLE", "l", true, true, false,
              true, true, &Functions::VarianceSample)},
    {"VARIANCE_POPULATION",
     Function("VARIANCE_POPULATION", "AQL_VARIANCE_POPULATION", "l", true, true,
              false, true, true, &Functions::VariancePopulation)},
    {"VARIANCE",
     Function(
         "VARIANCE", "AQL_VARIANCE_POPULATION", "l", true, true, false, true,
         true,
         &Functions::VariancePopulation)},  // alias for VARIANCE_POPULATION()
    {"STDDEV_SAMPLE",
     Function("STDDEV_SAMPLE", "AQL_STDDEV_SAMPLE", "l", true, true, false,
              true, true, &Functions::StdDevSample)},
    {"STDDEV_POPULATION",
     Function("STDDEV_POPULATION", "AQL_STDDEV_POPULATION", "l", true, true,
              false, true, true, &Functions::StdDevPopulation)},
    {"STDDEV",
     Function("STDDEV", "AQL_STDDEV_POPULATION", "l", true, true, false, true,
              true,
              &Functions::StdDevPopulation)},  // alias for STDDEV_POPULATION()
    {"UNIQUE", Function("UNIQUE", "AQL_UNIQUE", "l", true, true, false, true,
                        true, &Functions::Unique)},
    {"SORTED_UNIQUE",
     Function("SORTED_UNIQUE", "AQL_SORTED_UNIQUE", "l", true, true, false,
              true, true, &Functions::SortedUnique)},
    {"SLICE",
     Function("SLICE", "AQL_SLICE", "l,n|n", true, true, false, true, true, &Functions::Slice)},
    {"REVERSE",
     Function("REVERSE", "AQL_REVERSE", "ls", true, true, false, true,
              true)},  // note: REVERSE() can be applied on strings, too
    {"FIRST", Function("FIRST", "AQL_FIRST", "l", true, true, false, true, true,
                       &Functions::First)},
    {"LAST", Function("LAST", "AQL_LAST", "l", true, true, false, true, true,
                      &Functions::Last)},
    {"NTH", Function("NTH", "AQL_NTH", "l,n", true, true, false, true, true,
                     &Functions::Nth)},
    {"POSITION", Function("POSITION", "AQL_POSITION", "l,.|b", true, true,
                          false, true, true, &Functions::Position)},
    {"CALL",
     Function("CALL", "AQL_CALL", "s|.+", false, false, true, false, true)},
    {"APPLY",
     Function("APPLY", "AQL_APPLY", "s|l", false, false, true, false, false)},
    {"PUSH", Function("PUSH", "AQL_PUSH", "l,.|b", true, true, false, true,
                      false, &Functions::Push)},
    {"APPEND", Function("APPEND", "AQL_APPEND", "l,lz|b", true, true, false,
                        true, true, &Functions::Append)},
    {"POP", Function("POP", "AQL_POP", "l", true, true, false, true, true,
                     &Functions::Pop)},
    {"SHIFT", Function("SHIFT", "AQL_SHIFT", "l", true, true, false, true, true,
                       &Functions::Shift)},
    {"UNSHIFT", Function("UNSHIFT", "AQL_UNSHIFT", "l,.|b", true, true, false,
                         true, true, &Functions::Unshift)},
    {"REMOVE_VALUE",
     Function("REMOVE_VALUE", "AQL_REMOVE_VALUE", "l,.|n", true, true, false,
              true, true, &Functions::RemoveValue)},
    {"REMOVE_VALUES",
     Function("REMOVE_VALUES", "AQL_REMOVE_VALUES", "l,lz", true, true, false,
              true, true, &Functions::RemoveValues)},
    {"REMOVE_NTH", Function("REMOVE_NTH", "AQL_REMOVE_NTH", "l,n", true, true,
                            false, true, true, &Functions::RemoveNth)},

    // document functions
    {"HAS", Function("HAS", "AQL_HAS", "az,s", true, true, false, true, true,
                     &Functions::Has)},
    {"ATTRIBUTES", Function("ATTRIBUTES", "AQL_ATTRIBUTES", "a|b,b", true, true,
                            false, true, true, &Functions::Attributes)},
    {"VALUES", Function("VALUES", "AQL_VALUES", "a|b", true, true, false, true,
                        true, &Functions::Values)},
    {"MERGE", Function("MERGE", "AQL_MERGE", "la|+", true, true, false, true,
                       true, &Functions::Merge)},
    {"MERGE_RECURSIVE",
     Function("MERGE_RECURSIVE", "AQL_MERGE_RECURSIVE", "a,a|+", true, true,
              false, true, true, &Functions::MergeRecursive)},
    {"DOCUMENT",
     Function("DOCUMENT", "AQL_DOCUMENT", "h.|.", false, false, true, false,
              true, &Functions::Document, NotInCluster)},
    {"MATCHES", Function("MATCHES", "AQL_MATCHES", ".,l|b", true, true, false,
                         true, true)},
    {"UNSET", Function("UNSET", "AQL_UNSET", "a,sl|+", true, true, false, true,
                       true, &Functions::Unset)},
    {"UNSET_RECURSIVE",
     Function("UNSET_RECURSIVE", "AQL_UNSET_RECURSIVE", "a,sl|+", true, true,
              false, true, true, &Functions::UnsetRecursive)},
    {"KEEP", Function("KEEP", "AQL_KEEP", "a,sl|+", true, true, false, true,
                      true, &Functions::Keep)},
    {"TRANSLATE", Function("TRANSLATE", "AQL_TRANSLATE", ".,a|.", true, true,
                           false, true, true)},
    {"ZIP", Function("ZIP", "AQL_ZIP", "l,l", true, true, false, true, true,
                     &Functions::Zip)},

    // geo functions
    {"NEAR", Function("NEAR", "AQL_NEAR", "hs,n,n|nz,s", true, false, true,
                      false, true, &Functions::Near, NotInCoordinator)},
    {"WITHIN", Function("WITHIN", "AQL_WITHIN", "hs,n,n,n|s", true, false, true,
                        false, true, &Functions::Within, NotInCoordinator)},
    {"WITHIN_RECTANGLE",
     Function("WITHIN_RECTANGLE", "AQL_WITHIN_RECTANGLE", "hs,d,d,d,d", true,
              false, true, false, true)},
    {"IS_IN_POLYGON", Function("IS_IN_POLYGON", "AQL_IS_IN_POLYGON", "l,ln|nb",
                               true, true, false, true, true)},

    // fulltext functions
    {"FULLTEXT",
     Function("FULLTEXT", "AQL_FULLTEXT", "hs,s,s|n", true, false, true, false,
              true, &Functions::Fulltext, NotInCoordinator)},

    // graph functions
    {"PATHS", Function("PATHS", "AQL_PATHS", "c,h|s,ba", true, false, true,
                       false, false)},
    {"GRAPH_PATHS", Function("GRAPH_PATHS", "AQL_GRAPH_PATHS", "s|a", false,
                             false, true, false, false)},
    {"SHORTEST_PATH", Function("SHORTEST_PATH", "AQL_SHORTEST_PATH",
                               "h,h,s,s,s|a", true, false, true, false, false)},
    {"GRAPH_SHORTEST_PATH",
     Function("GRAPH_SHORTEST_PATH", "AQL_GRAPH_SHORTEST_PATH", "s,als,als|a",
              false, false, true, false, false)},
    {"GRAPH_DISTANCE_TO",
     Function("GRAPH_DISTANCE_TO", "AQL_GRAPH_DISTANCE_TO", "s,als,als|a",
              false, false, true, false, false)},
    {"TRAVERSAL", Function("TRAVERSAL", "AQL_TRAVERSAL", "hs,hs,s,s|a", false,
                           false, true, false, false)},
    {"GRAPH_TRAVERSAL",
     Function("GRAPH_TRAVERSAL", "AQL_GRAPH_TRAVERSAL", "s,als,s|a", false,
              false, true, false, false)},
    {"TRAVERSAL_TREE",
     Function("TRAVERSAL_TREE", "AQL_TRAVERSAL_TREE", "hs,hs,s,s,s|a", false,
              false, true, false, false)},
    {"GRAPH_TRAVERSAL_TREE",
     Function("GRAPH_TRAVERSAL_TREE", "AQL_GRAPH_TRAVERSAL_TREE", "s,als,s,s|a",
              false, false, true, false, false)},
    {"EDGES", Function("EDGES", "AQL_EDGES", "hs,s,s|l,o", true, false, true,
                       false, false, &Functions::Edges, NotInCluster)},
    {"GRAPH_EDGES", Function("GRAPH_EDGES", "AQL_GRAPH_EDGES", "s,als|a", false,
                             false, true, false, false)},
    {"GRAPH_VERTICES", Function("GRAPH_VERTICES", "AQL_GRAPH_VERTICES",
                                "s,als|a", false, false, true, false, false)},
    {"NEIGHBORS",
     Function("NEIGHBORS", "AQL_NEIGHBORS", "hs,hs,s,s|l,a", true, false, true,
              false, false, &Functions::Neighbors, NotInCluster)},
    {"GRAPH_NEIGHBORS", Function("GRAPH_NEIGHBORS", "AQL_GRAPH_NEIGHBORS",
                                 "s,als|a", false, false, true, false, false)},
    {"GRAPH_COMMON_NEIGHBORS",
     Function("GRAPH_COMMON_NEIGHBORS", "AQL_GRAPH_COMMON_NEIGHBORS",
              "s,als,als|a,a", false, false, true, false, false)},
    {"GRAPH_COMMON_PROPERTIES",
     Function("GRAPH_COMMON_PROPERTIES", "AQL_GRAPH_COMMON_PROPERTIES",
              "s,als,als|a", false, false, true, false, false)},
    {"GRAPH_ECCENTRICITY",
     Function("GRAPH_ECCENTRICITY", "AQL_GRAPH_ECCENTRICITY", "s|a", false,
              false, true, false, false)},
    {"GRAPH_BETWEENNESS", Function("GRAPH_BETWEENNESS", "AQL_GRAPH_BETWEENNESS",
                                   "s|a", false, false, true, false, false)},
    {"GRAPH_CLOSENESS", Function("GRAPH_CLOSENESS", "AQL_GRAPH_CLOSENESS",
                                 "s|a", false, false, true, false, false)},
    {"GRAPH_ABSOLUTE_ECCENTRICITY",
     Function("GRAPH_ABSOLUTE_ECCENTRICITY", "AQL_GRAPH_ABSOLUTE_ECCENTRICITY",
              "s,als|a", false, false, true, false, false)},
    {"GRAPH_ABSOLUTE_BETWEENNESS",
     Function("GRAPH_ABSOLUTE_BETWEENNESS", "AQL_GRAPH_ABSOLUTE_BETWEENNESS",
              "s,als|a", false, false, true, false, false)},
    {"GRAPH_ABSOLUTE_CLOSENESS",
     Function("GRAPH_ABSOLUTE_CLOSENESS", "AQL_GRAPH_ABSOLUTE_CLOSENESS",
              "s,als|a", false, false, true, false, false)},
    {"GRAPH_DIAMETER", Function("GRAPH_DIAMETER", "AQL_GRAPH_DIAMETER", "s|a",
                                false, false, true, false, false)},
    {"GRAPH_RADIUS", Function("GRAPH_RADIUS", "AQL_GRAPH_RADIUS", "s|a", false,
                              false, true, false, false)},

    // date functions
    {"DATE_NOW",
     Function("DATE_NOW", "AQL_DATE_NOW", "", false, false, false, true, true)},
    {"DATE_TIMESTAMP",
     Function("DATE_TIMESTAMP", "AQL_DATE_TIMESTAMP", "ns|ns,ns,ns,ns,ns,ns",
              true, true, false, true, true)},
    {"DATE_ISO8601",
     Function("DATE_ISO8601", "AQL_DATE_ISO8601", "ns|ns,ns,ns,ns,ns,ns", true,
              true, false, true, true)},
    {"DATE_DAYOFWEEK", Function("DATE_DAYOFWEEK", "AQL_DATE_DAYOFWEEK", "ns",
                                true, true, false, true, true)},
    {"DATE_YEAR", Function("DATE_YEAR", "AQL_DATE_YEAR", "ns", true, true,
                           false, true, true)},
    {"DATE_MONTH", Function("DATE_MONTH", "AQL_DATE_MONTH", "ns", true, true,
                            false, true, true)},
    {"DATE_DAY",
     Function("DATE_DAY", "AQL_DATE_DAY", "ns", true, true, false, true, true)},
    {"DATE_HOUR", Function("DATE_HOUR", "AQL_DATE_HOUR", "ns", true, true,
                           false, true, true)},
    {"DATE_MINUTE", Function("DATE_MINUTE", "AQL_DATE_MINUTE", "ns", true, true,
                             false, true, true)},
    {"DATE_SECOND", Function("DATE_SECOND", "AQL_DATE_SECOND", "ns", true, true,
                             false, true, true)},
    {"DATE_MILLISECOND", Function("DATE_MILLISECOND", "AQL_DATE_MILLISECOND",
                                  "ns", true, true, false, true, true)},
    {"DATE_DAYOFYEAR", Function("DATE_DAYOFYEAR", "AQL_DATE_DAYOFYEAR", "ns",
                                true, true, false, true, true)},
    {"DATE_ISOWEEK", Function("DATE_ISOWEEK", "AQL_DATE_ISOWEEK", "ns", true,
                              true, false, true, true)},
    {"DATE_LEAPYEAR", Function("DATE_LEAPYEAR", "AQL_DATE_LEAPYEAR", "ns", true,
                               true, false, true, true)},
    {"DATE_QUARTER", Function("DATE_QUARTER", "AQL_DATE_QUARTER", "ns", true,
                              true, false, true, true)},
    {"DATE_DAYS_IN_MONTH",
     Function("DATE_DAYS_IN_MONTH", "AQL_DATE_DAYS_IN_MONTH", "ns", true, true,
              false, true, true)},
    {"DATE_ADD", Function("DATE_ADD", "AQL_DATE_ADD", "ns,ns|n", true, true,
                          false, true, true)},
    {"DATE_SUBTRACT", Function("DATE_SUBTRACT", "AQL_DATE_SUBTRACT", "ns,ns|n",
                               true, true, false, true, true)},
    {"DATE_DIFF", Function("DATE_DIFF", "AQL_DATE_DIFF", "ns,ns,s|b", true,
                           true, false, true, true)},
    {"DATE_COMPARE", Function("DATE_COMPARE", "AQL_DATE_COMPARE", "ns,ns,s|s",
                              true, true, false, true, true)},
    {"DATE_FORMAT", Function("DATE_FORMAT", "AQL_DATE_FORMAT", "ns,s", true,
                             true, false, true, true)},

    // misc functions
    {"FAIL",
     Function("FAIL", "AQL_FAIL", "|s", false, false, true, true, true)},
    {"PASSTHRU", Function("PASSTHRU", "AQL_PASSTHRU", ".", false, false, false,
                          true, true, &Functions::Passthru)},
    {"NOOPT", Function("NOOPT", "AQL_PASSTHRU", ".", false, false, false, true,
                       true, &Functions::Passthru)},
    {"V8",
     Function("V8", "AQL_PASSTHRU", ".", false, true, false, true, true)},
    {"TEST_INTERNAL", Function("TEST_INTERNAL", "AQL_TEST_INTERNAL", "s,.",
                               false, false, false, true, false)},
    {"SLEEP",
     Function("SLEEP", "AQL_SLEEP", "n", false, false, true, true, true)},
    {"COLLECTIONS", Function("COLLECTIONS", "AQL_COLLECTIONS", "", false, false,
                             true, false, true)},
    {"NOT_NULL", Function("NOT_NULL", "AQL_NOT_NULL", ".|+", true, true, false,
                          true, true, &Functions::NotNull)},
    {"FIRST_LIST", Function("FIRST_LIST", "AQL_FIRST_LIST", ".|+", true, true,
                            false, true, true, &Functions::FirstList)},
    {"FIRST_DOCUMENT",
     Function("FIRST_DOCUMENT", "AQL_FIRST_DOCUMENT", ".|+", true, true, false,
              true, true, &Functions::FirstDocument)},
    {"PARSE_IDENTIFIER",
     Function("PARSE_IDENTIFIER", "AQL_PARSE_IDENTIFIER", ".", true, true,
              false, true, true, &Functions::ParseIdentifier)},
    {"IS_SAME_COLLECTION",
      Function("IS_SAME_COLLECTION", "AQL_IS_SAME_COLLECTION", "ch,as", true, true,
               false, true, true, &Functions::IsSameCollection)},
    {"CURRENT_USER", Function("CURRENT_USER", "AQL_CURRENT_USER", "", false,
                              false, false, false, true)},
    {"CURRENT_DATABASE",
     Function("CURRENT_DATABASE", "AQL_CURRENT_DATABASE", "", false, false,
              false, false, true, &Functions::CurrentDatabase)},
    {"COLLECTION_COUNT",
     Function("COLLECTION_COUNT", "AQL_COLLECTION_COUNT", "chs", false, false,
              true, false, true, &Functions::CollectionCount, NotInCluster)}};
