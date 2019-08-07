/*
 * Copyright 2014 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef REALM_JAVA_UTIL_HPP
#define REALM_JAVA_UTIL_HPP

#include <string>
#include <sstream>
#include <memory>

#include <jni.h>

// Used by logging
#include <inttypes.h>

#include <realm.hpp>
#include <realm/timestamp.hpp>
#include <realm/table.hpp>
#include <realm/util/safe_int_ops.hpp>
#include "io_realm_internal_Util.h"

#include "java_exception_def.hpp"
#include "jni_util/log.hpp"
#include "jni_util/java_exception_thrower.hpp"

#define CHECK_PARAMETERS 1 // Check all parameters in API and throw exceptions in java if invalid

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved);

#ifdef __cplusplus
}
#endif

// Exception handling
#define CATCH_STD()                                                                                                  \
    catch (...)                                                                                                      \
    {                                                                                                                \
        ConvertException(env, __FILE__, __LINE__);                                                                   \
    }


#define MAX_JINT 0x7FFFFFFFL
#define MAX_JSIZE MAX_JINT

// TODO: Clean up those marcos. Casting with marcos reduces the readability, and it is actually breaking the C++ type
// conversion. e.g.: You cannot cast a pointer with S64 below.
// Helper macros for better readability
#define S(x) static_cast<size_t>(x)
#define B(x) static_cast<bool>(x)
#define Q(x) reinterpret_cast<realm::Query*>(x)
#define ROW(x) reinterpret_cast<realm::Obj*>(x)//TODO rename ROW to OBJ
#define TBL_REF(x) *reinterpret_cast<realm::TableRef*>(x)

// Exception handling
enum ExceptionKind {
    // FIXME: This is not something should be exposed to java, ClassNotFound is something we should
    // crash hard in native code and fix it.
    ClassNotFound = 0,
    IllegalArgument,
    IndexOutOfBounds,
    UnsupportedOperation,
    OutOfMemory,
    FatalError,
    RuntimeError,
    BadVersion,
    IllegalState,
    RealmFileError,
    // NOTE!!!!: Please also add test cases to io_realm_internal_TestUtil when introducing a
    // new exception kind.
    ExceptionKindMax // Always keep this as the last one!
};

void ConvertException(JNIEnv* env, const char* file, int line);
void ThrowException(JNIEnv* env, ExceptionKind exception, const std::string& classStr,
                    const std::string& itemStr = "");
void ThrowException(JNIEnv* env, ExceptionKind exception, const char* classStr);
void ThrowNullValueException(JNIEnv* env, realm::Table* table, realm::ColKey col_key);

// Check parameters

#define TABLE_VALID(env, ptr) TableIsValid(env, ptr)
#define ROW_VALID(env, ptr) RowIsValid(env, ptr)

#if CHECK_PARAMETERS

#define TYPE_VALID(env, ptr, col, type) TypeValid(env, ptr, col, type)
#define COL_NULLABLE(env, ptr, columnKey) ColIsNullable(env, ptr, columnKey)

#else

#define TYPE_VALID(env, ptr, col, type) (true)
#define COL_NULLABLE(env, ptr, col) (true)

#endif


inline jlong to_jlong_or_not_found(size_t res)
{
    return (res == realm::not_found) ? jlong(-1) : jlong(res);
}

inline jlong to_jlong_or_not_found(realm::ColKey key)
{
    return bool(key) ? jlong(key.value) : jlong(-1);
}

inline jlong to_jlong_or_not_found(realm::ObjKey key)
{
    return bool(key) ? jlong(key.value) : jlong(-1);
}

inline bool TableIsValid(JNIEnv* env, const realm::TableRef& table)
{
    if (!table) {
        realm::jni_util::Log::e("Table is no longer attached!");
        ThrowException(env, IllegalState, "Table is no longer valid to operate on.");
    }
    return true;
}

inline bool RowIsValid(JNIEnv* env, realm::Obj* rowPtr)
{
    bool valid = (rowPtr != NULL && rowPtr->is_valid());
    if (!valid) {
        realm::jni_util::Log::e("Row %1 is no longer attached!", reinterpret_cast<int64_t>(rowPtr));
        ThrowException(env, IllegalState,
                       "Object is no longer valid to operate on. Was it deleted by another thread?");
    }
    return valid;
}

template <class T>
inline bool TypeValid(JNIEnv* env, T* pTable, jlong columnIndex, int expectColType)
{
    realm::ColKey col_key(columnIndex);
    int colType = pTable->get_column_type(col_key);
    if (colType != expectColType) {
        realm::jni_util::Log::e("Expected columnType %1, but got %2.", expectColType, colType);
        ThrowException(env, IllegalArgument, "ColumnType of '" + std::string(pTable->get_column_name(col_key)) + "' is invalid.");
        return false;
    }
    return true;
}

template <class T>
inline bool ColIsNullable(JNIEnv* env, T* pTable, jlong columnKey)
{
    realm::ColKey col = realm::ColKey(columnKey);
    int colType = pTable->get_column_type(col);
    if (colType == realm::type_Link) {
        return true;
    }

    if (colType == realm::type_LinkList) {
        ThrowException(env, IllegalArgument, "RealmList(" + std::string(pTable->get_column_name(col)) + ") is not nullable.");
        return false;
    }

    // checking for primitive list
    if (pTable->is_list(col)) {
        ThrowException(env, IllegalArgument, "RealmList(" + std::string(pTable->get_column_name(col)) + ") is not nullable.");
        return false;
    }

    if (pTable->is_nullable(col)) {
        return true;
    }

    realm::jni_util::Log::e("Expected nullable column type");
    ThrowException(env, IllegalArgument, "This field(" + std::string(pTable->get_column_name(col)) + ") is not nullable.");
    return false;
}

// Utility function for appending StringData, which is returned
// by a lot of core functions, and might potentially be NULL.
std::string concat_stringdata(const char* message, realm::StringData data);

// Note: JNI offers methods to convert between modified UTF-8 and
// UTF-16. Unfortunately these methods are not appropriate in this
// context. The reason is that they use a modified version of
// UTF-8 where U+0000 is stored as 0xC0 0x80 instead of 0x00 and
// where a character in the range U+10000 to U+10FFFF is stored as
// two consecutive UTF-8 encodings of the corresponding UTF-16
// surrogate pair. Because Realm uses proper UTF-8, we need to
// do the transcoding ourselves.
//
// See also http://en.wikipedia.org/wiki/UTF-8#Modified_UTF-8

jstring to_jstring(JNIEnv*, realm::StringData);

class JStringAccessor {
public:
    JStringAccessor(JNIEnv*, jstring); // throws

    bool is_null_or_empty() {
        return m_is_null || m_size == 0;
    }

    operator realm::StringData() const
    {
        // To solve the link issue by directly using Table::max_string_size
        static constexpr size_t max_string_size = realm::Table::max_string_size;

        if (m_is_null) {
            return realm::StringData();
        }
        else if (m_size > max_string_size) {
            THROW_JAVA_EXCEPTION(
                m_env, realm::_impl::JavaExceptionDef::IllegalArgument,
                realm::util::format(
                    "The length of 'String' value in UTF8 encoding is %1 which exceeds the max string length %2.",
                    m_size, max_string_size));
        }
        else {
            return realm::StringData(m_data.get(), m_size);
        }
    }

    operator std::string() const noexcept
    {
        if (m_is_null) {
            return std::string();
        }
        return std::string(m_data.get(), m_size);
    }

private:
    JNIEnv* m_env;
    bool m_is_null;
    std::shared_ptr<char> m_data;
    std::size_t m_size;
};

inline jlong to_milliseconds(const realm::Timestamp& ts)
{
    // From core's reference implementation aka unit test
    // FIXME: check for overflow/underflow
    const int64_t seconds = ts.get_seconds();
    const int32_t nanoseconds = ts.get_nanoseconds();
    const int64_t milliseconds = seconds * 1000 + nanoseconds / 1000000; // This may overflow
    return milliseconds;
}

inline realm::Timestamp from_milliseconds(jlong milliseconds)
{
    // From core's reference implementation aka unit test
    int64_t seconds = milliseconds / 1000;
    int32_t nanoseconds = (milliseconds % 1000) * 1000000;
    return realm::Timestamp(seconds, nanoseconds);
}

extern const std::string TABLE_PREFIX;

static inline bool to_bool(jboolean b)
{
    return b == JNI_TRUE;
}

static inline jboolean to_jbool(bool b)
{
    return b ? JNI_TRUE : JNI_FALSE;
}

#endif // REALM_JAVA_UTIL_HPP
