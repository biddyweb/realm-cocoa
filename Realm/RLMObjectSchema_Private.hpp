////////////////////////////////////////////////////////////////////////////
//
// Copyright 2014 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#import "RLMObjectSchema.h"

#import <tightdb/table.hpp>

// RLMObjectSchema private
@interface RLMObjectSchema ()
// writable redecleration
@property (nonatomic, readwrite, copy) NSArray * properties;

// class used for this object schema
@property (nonatomic, readwrite, assign) Class objectClass;

// returns a cached or new schema for a given object class
+(instancetype)schemaForObjectClass:(Class)objectClass;

// generate a schema from a table
+(instancetype)schemaForTable:(tightdb::Table *)table className:(NSString *)className;

@end