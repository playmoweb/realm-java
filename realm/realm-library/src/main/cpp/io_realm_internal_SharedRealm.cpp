/*
 * Copyright 2016 Realm Inc.
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

#include "io_realm_internal_SharedRealm.h"
#if REALM_ENABLE_SYNC
#include "object-store/src/sync/sync_manager.hpp"
#include "object-store/src/sync/sync_config.hpp"
#include "object-store/src/sync/sync_session.hpp"
#endif

#include <realm/util/assert.hpp>

#include <shared_realm.hpp>

#include "java_accessor.hpp"
#include "java_binding_context.hpp"
#include "java_exception_def.hpp"
#include "object_store.hpp"
#include "util.hpp"
#include "jni_util/java_method.hpp"
#include "jni_util/java_class.hpp"
#include "jni_util/java_exception_thrower.hpp"


using namespace realm;
using namespace realm::_impl;
using namespace realm::jni_util;

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeInit(JNIEnv* env, jclass,
                                                                     jstring temporary_directory_path)
{
    TR_ENTER()

    try {
        JStringAccessor path(env, temporary_directory_path);    // throws
        SharedGroupOptions::set_sys_tmp_dir(std::string(path)); // throws
    }
    CATCH_STD()
}

JNIEXPORT jlong JNICALL Java_io_realm_internal_SharedRealm_nativeGetSharedRealm(JNIEnv* env, jclass, jlong config_ptr,
                                                                                jobject realm_notifier)
{
    TR_ENTER_PTR(config_ptr)

    auto& config = *reinterpret_cast<Realm::Config*>(config_ptr);
    try {
        auto shared_realm = Realm::get_shared_realm(config);
        // The migration callback & initialization callback could throw.
        if (env->ExceptionCheck()) {
            return reinterpret_cast<jlong>(nullptr);
        }
        shared_realm->m_binding_context = JavaBindingContext::create(env, realm_notifier);
        return reinterpret_cast<jlong>(new SharedRealm(std::move(shared_realm)));
    }
    catch (SchemaMismatchException& e) {
        // An exception has been thrown in the migration block.
        if (env->ExceptionCheck()) {
            return reinterpret_cast<jlong>(nullptr);
        }
        static JavaClass migration_needed_class(env, JavaExceptionDef::RealmMigrationNeeded);
        static JavaMethod constructor(env, migration_needed_class, "<init>",
                                      "(Ljava/lang/String;Ljava/lang/String;)V");

        jstring message = to_jstring(env, e.what());
        jstring path = to_jstring(env, config.path);
        jobject migration_needed_exception = env->NewObject(migration_needed_class, constructor, path, message);
        env->Throw(reinterpret_cast<jthrowable>(migration_needed_exception));
    }
    catch (InvalidSchemaVersionException& e) {
        // An exception has been thrown in the migration block.
        if (env->ExceptionCheck()) {
            return reinterpret_cast<jlong>(nullptr);
        }
        // To match the old behaviour. Otherwise it will be converted to ISE in the CATCH_STD.
        ThrowException(env, IllegalArgument, e.what());
    }
    CATCH_STD()
    return reinterpret_cast<jlong>(nullptr);
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeCloseSharedRealm(JNIEnv*, jclass,
                                                                                 jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    // Close the SharedRealm only. Let the finalizer daemon thread free the SharedRealm
    if (!shared_realm->is_closed()) {
        shared_realm->close();
    }
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeBeginTransaction(JNIEnv* env, jclass,
                                                                                 jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        shared_realm->begin_transaction();
    }
    CATCH_STD()
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeCommitTransaction(JNIEnv* env, jclass,
                                                                                  jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        shared_realm->commit_transaction();
        // Realm could be closed in the RealmNotifier.didChange().
        if (!shared_realm->is_closed()) {
            // To trigger async queries, so the UI can be refreshed immediately to avoid inconsistency.
            // See more discussion on https://github.com/realm/realm-java/issues/4245
            shared_realm->refresh();
        }
    }
    CATCH_STD()
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeCancelTransaction(JNIEnv* env, jclass,
                                                                                  jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        shared_realm->cancel_transaction();
    }
    CATCH_STD()
}


JNIEXPORT jboolean JNICALL Java_io_realm_internal_SharedRealm_nativeIsInTransaction(JNIEnv*, jclass,
                                                                                    jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    return static_cast<jboolean>(shared_realm->is_in_transaction());
}

JNIEXPORT jlong JNICALL Java_io_realm_internal_SharedRealm_nativeReadGroup(JNIEnv* env, jclass,
                                                                           jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        return reinterpret_cast<jlong>(&shared_realm->read_group());
    }
    CATCH_STD()

    return static_cast<jlong>(NULL);
}

JNIEXPORT jlong JNICALL Java_io_realm_internal_SharedRealm_nativeGetVersion(JNIEnv* env, jclass,
                                                                            jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        return static_cast<jlong>(ObjectStore::get_schema_version(shared_realm->read_group()));
    }
    CATCH_STD()
    return static_cast<jlong>(ObjectStore::NotVersioned);
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeSetVersion(JNIEnv* env, jclass,
                                                                           jlong shared_realm_ptr, jlong version)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        if (!shared_realm->is_in_transaction()) {
            std::ostringstream ss;
            ss << "Cannot set schema version when the realm is not in transaction.";
            ThrowException(env, IllegalState, ss.str());
            return;
        }

        ObjectStore::set_schema_version(shared_realm->read_group(), static_cast<uint64_t>(version));
    }
    CATCH_STD()
}

JNIEXPORT jboolean JNICALL Java_io_realm_internal_SharedRealm_nativeIsEmpty(JNIEnv* env, jclass,
                                                                            jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        return static_cast<jboolean>(ObjectStore::is_empty(shared_realm->read_group()));
    }
    CATCH_STD()
    return JNI_FALSE;
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeRefresh(JNIEnv* env, jclass, jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        shared_realm->refresh();
    }
    CATCH_STD()
}

JNIEXPORT jlongArray JNICALL Java_io_realm_internal_SharedRealm_nativeGetVersionID(JNIEnv* env, jclass,
                                                                                   jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        using rf = realm::_impl::RealmFriend;
        SharedGroup::VersionID version_id = rf::get_shared_group(*shared_realm).get_version_of_current_transaction();

        jlong version_array[2];
        version_array[0] = static_cast<jlong>(version_id.version);
        version_array[1] = static_cast<jlong>(version_id.index);

        jlongArray version_data = env->NewLongArray(2);
        if (version_data == NULL) {
            ThrowException(env, OutOfMemory, "Could not allocate memory to return versionID.");
            return NULL;
        }
        env->SetLongArrayRegion(version_data, 0, 2, version_array);

        return version_data;
    }
    CATCH_STD()

    return NULL;
}

JNIEXPORT jboolean JNICALL Java_io_realm_internal_SharedRealm_nativeIsClosed(JNIEnv*, jclass, jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    return static_cast<jboolean>(shared_realm->is_closed());
}


JNIEXPORT jlong JNICALL Java_io_realm_internal_SharedRealm_nativeGetTable(JNIEnv* env, jclass, jlong shared_realm_ptr,
                                                                          jstring table_name)
{
    TR_ENTER_PTR(shared_realm_ptr)

    try {
        JStringAccessor name(env, table_name); // throws
        auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
        if (!shared_realm->read_group().has_table(name)) {
            std::string name_str = name;
            if (name_str.find(TABLE_PREFIX) == 0) {
                name_str = name_str.substr(TABLE_PREFIX.length());
            }
            THROW_JAVA_EXCEPTION(env, JavaExceptionDef::IllegalArgument,
                                 format("The class '%1' doesn't exist in this Realm.", name_str));
        }
        Table* table = LangBindHelper::get_table(shared_realm->read_group(), name);
        return reinterpret_cast<jlong>(table);
    }
    CATCH_STD()

    return reinterpret_cast<jlong>(nullptr);
}

JNIEXPORT jlong JNICALL Java_io_realm_internal_SharedRealm_nativeCreateTable(JNIEnv* env, jclass,
                                                                             jlong shared_realm_ptr,
                                                                             jstring table_name)
{
    TR_ENTER_PTR(shared_realm_ptr)

    std::string name_str;
    try {
        JStringAccessor name(env, table_name); // throws
        name_str = name;
        auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
        shared_realm->verify_in_write(); // throws
        Table* table = LangBindHelper::add_table(shared_realm->read_group(), name); // throws
        return reinterpret_cast<jlong>(table);
    }
    catch (TableNameInUse& e) {
        // We need to print the table name, so catch the exception here.
        ThrowException(env, IllegalArgument, format("Class already exists: '%1'.", name_str));
    }
    CATCH_STD()

    return reinterpret_cast<jlong>(nullptr);
}

JNIEXPORT jstring JNICALL Java_io_realm_internal_SharedRealm_nativeGetTableName(JNIEnv* env, jclass,
                                                                                jlong shared_realm_ptr, jint index)
{

    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        return to_jstring(env, shared_realm->read_group().get_table_name(static_cast<size_t>(index)));
    }
    CATCH_STD()
    return NULL;
}

JNIEXPORT jboolean JNICALL Java_io_realm_internal_SharedRealm_nativeHasTable(JNIEnv* env, jclass,
                                                                             jlong shared_realm_ptr,
                                                                             jstring table_name)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        JStringAccessor name(env, table_name);
        return static_cast<jboolean>(shared_realm->read_group().has_table(name));
    }
    CATCH_STD()
    return JNI_FALSE;
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeRenameTable(JNIEnv* env, jclass,
                                                                            jlong shared_realm_ptr,
                                                                            jstring old_table_name,
                                                                            jstring new_table_name)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        JStringAccessor old_name(env, old_table_name);
        if (!shared_realm->is_in_transaction()) {
            std::ostringstream ss;
            ss << "Class " << old_name << " cannot be removed when the realm is not in transaction.";
            ThrowException(env, IllegalState, ss.str());
            return;
        }
        JStringAccessor new_name(env, new_table_name);
        shared_realm->read_group().rename_table(old_name, new_name);
    }
    CATCH_STD()
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeRemoveTable(JNIEnv* env, jclass,
                                                                            jlong shared_realm_ptr,
                                                                            jstring table_name)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        JStringAccessor name(env, table_name);
        if (!shared_realm->is_in_transaction()) {
            std::ostringstream ss;
            ss << "Class " << name << " cannot be removed when the realm is not in transaction.";
            ThrowException(env, IllegalState, ss.str());
            return;
        }
        shared_realm->read_group().remove_table(name);
    }
    CATCH_STD()
}

JNIEXPORT jlong JNICALL Java_io_realm_internal_SharedRealm_nativeSize(JNIEnv* env, jclass, jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        return static_cast<jlong>(shared_realm->read_group().size());
    }
    CATCH_STD()

    return 0;
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeWriteCopy(JNIEnv* env, jclass, jlong shared_realm_ptr,
                                                                          jstring path, jbyteArray key)
{
    TR_ENTER_PTR(shared_realm_ptr);

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        JStringAccessor path_str(env, path);
        JByteArrayAccessor jarray_accessor(env, key);
        shared_realm->write_copy(path_str, jarray_accessor.transform<BinaryData>());
    }
    CATCH_STD()
}

JNIEXPORT jboolean JNICALL Java_io_realm_internal_SharedRealm_nativeWaitForChange(JNIEnv* env, jclass,
                                                                                  jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr);

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        using rf = realm::_impl::RealmFriend;
        return static_cast<jboolean>(rf::get_shared_group(*shared_realm).wait_for_change());
    }
    CATCH_STD()

    return JNI_FALSE;
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeStopWaitForChange(JNIEnv* env, jclass,
                                                                                  jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr);

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        using rf = realm::_impl::RealmFriend;
        rf::get_shared_group(*shared_realm).wait_for_change_release();
    }
    CATCH_STD()
}

JNIEXPORT jboolean JNICALL Java_io_realm_internal_SharedRealm_nativeCompact(JNIEnv* env, jclass,
                                                                            jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr);

    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    try {
        return static_cast<jboolean>(shared_realm->compact());
    }
    CATCH_STD()

    return JNI_FALSE;
}

static void finalize_shared_realm(jlong ptr)
{
    TR_ENTER_PTR(ptr)
    delete reinterpret_cast<SharedRealm*>(ptr);
}

JNIEXPORT jlong JNICALL Java_io_realm_internal_SharedRealm_nativeGetFinalizerPtr(JNIEnv*, jclass)
{
    TR_ENTER()
    return reinterpret_cast<jlong>(&finalize_shared_realm);
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeSetAutoRefresh(JNIEnv* env, jclass,
                                                                               jlong shared_realm_ptr,
                                                                               jboolean enabled)
{
    TR_ENTER_PTR(shared_realm_ptr)
    try {
        auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
        shared_realm->set_auto_refresh(to_bool(enabled));
    }
    CATCH_STD()
}

JNIEXPORT jboolean JNICALL Java_io_realm_internal_SharedRealm_nativeIsAutoRefresh(JNIEnv* env, jclass,
                                                                                  jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)
    try {
        auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
        return to_jbool(shared_realm->auto_refresh());
    }
    CATCH_STD()
    return JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_io_realm_internal_SharedRealm_nativeGetSchemaInfo(JNIEnv*, jclass,
                                                                               jlong shared_realm_ptr)
{
    TR_ENTER_PTR(shared_realm_ptr)

    // No throws
    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    return reinterpret_cast<jlong>(&shared_realm->schema());
}

JNIEXPORT void JNICALL Java_io_realm_internal_SharedRealm_nativeRegisterSchemaChangedCallback(
    JNIEnv* env, jclass, jlong shared_realm_ptr, jobject j_schema_changed_callback)
{
    TR_ENTER_PTR(shared_realm_ptr)

    // No throws
    auto& shared_realm = *(reinterpret_cast<SharedRealm*>(shared_realm_ptr));
    JavaGlobalWeakRef callback_weak_ref(env, j_schema_changed_callback);
    if (shared_realm->m_binding_context) {
        JavaBindingContext& java_binding_context =
            *(static_cast<JavaBindingContext*>(shared_realm->m_binding_context.get()));
        java_binding_context.set_schema_changed_callback(env, j_schema_changed_callback);
    }
}
