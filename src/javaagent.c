#include "dbg.h"
#include "os.h"
#include "state.h"
#include <jni.h>
#include <jvmti.h>
#include "javabci.h"

typedef struct {
    jmethodID mid_Object_hashCode;
    jmethodID mid_SSLSocketImpl_getSession;
    
    jmethodID mid_AppOutputStream___write;
    jmethodID mid_AppInputStream___read;
    jfieldID  fid_AppOutputStream_socket;
    jfieldID  fid_AppInputStream_socket;
    
    jmethodID mid_ByteBuffer_array;
    jmethodID mid_ByteBuffer_position;
    jmethodID mid_ByteBuffer_limit;
    jfieldID  fid_ByteBuffer___fd;
    
    jmethodID mid_SSLEngineImpl___wrap;
    jmethodID mid_SSLEngineImpl___unwrap;
    jmethodID mid_SSLEngineImpl_getSession;
    
    jmethodID mid_Socket_getInetAddress;
    jmethodID mid_Socket_getPort;
    jmethodID mid_Socket_getImp;

    jmethodID mid_SocketImpl_getFileDescriptor;

    jfieldID  fid_FileDescriptor_fd;

    jmethodID mid_InetAddress_getHostAddress;
    
    jmethodID mid_SocketChannelImpl___read;
    jmethodID mid_SocketChannelImpl___write;
    jmethodID mid_SocketChannelImpl_getRemoteAddress;
    jmethodID mid_SocketChannelImpl___close;
    jmethodID mid_SocketChannelImpl_getFDVal;
} java_global_t;

static java_global_t g_java = {0};

static void 
logJvmtiError(jvmtiEnv *jvmti, jvmtiError errnum, const char *str) 
{
    char buf[1024];
    char *errnum_str = NULL;
    (*jvmti)->GetErrorName(jvmti, errnum, &errnum_str);
    snprintf(buf, sizeof(buf), "ERROR: JVMTI: [%d] %s - %s\n", errnum, (errnum_str == NULL ? "Unknown": errnum_str), (str == NULL? "" : str));
    scopeLog(buf, -1, CFG_LOG_ERROR);
}

static void 
clearJniException(JNIEnv *jni)
{
    jboolean flag = (*jni)->ExceptionCheck(jni);
    if (flag) (*jni)->ExceptionClear(jni);
}

static int 
getFdFromSocket(JNIEnv *jni, jobject socket)
{
    int fd = -1;
    jobject socketImpl = (*jni)->CallObjectMethod(jni, socket, g_java.mid_Socket_getImp);
    jobject fdObj = (*jni)->CallObjectMethod(jni, socketImpl, g_java.mid_SocketImpl_getFileDescriptor);
    if (fdObj) {
        fd = (*jni)->GetIntField(jni, fdObj, g_java.fid_FileDescriptor_fd);
    }
    return fd;
}

static void 
initJniGlobals(JNIEnv *jni) 
{
    if (g_java.mid_Object_hashCode != NULL) return;
    jclass objectClass             = (*jni)->FindClass(jni, "java/lang/Object");
    g_java.mid_Object_hashCode     = (*jni)->GetMethodID(jni, objectClass, "hashCode", "()I");

    jclass sslSocketImplClass      = (*jni)->FindClass(jni, "sun/security/ssl/SSLSocketImpl");
    if (sslSocketImplClass == NULL) {
        // Oracle JDK 6
        sslSocketImplClass = (*jni)->FindClass(jni, "com/sun/net/ssl/internal/ssl/SSLSocketImpl");
        clearJniException(jni);
    }
    g_java.mid_SSLSocketImpl_getSession = (*jni)->GetMethodID(jni, sslSocketImplClass, "getSession", "()Ljavax/net/ssl/SSLSession;");

    jclass socketClass         = (*jni)->FindClass(jni, "java/net/Socket");
    g_java.mid_Socket_getInetAddress = (*jni)->GetMethodID(jni, socketClass, "getInetAddress", "()Ljava/net/InetAddress;");
    g_java.mid_Socket_getPort      = (*jni)->GetMethodID(jni, socketClass, "getPort", "()I");
    g_java.mid_Socket_getImp       = (*jni)->GetMethodID(jni, socketClass, "getImpl", "()Ljava/net/SocketImpl;");

    jclass socketImplClass         = (*jni)->FindClass(jni, "java/net/SocketImpl");
    g_java.mid_SocketImpl_getFileDescriptor = (*jni)->GetMethodID(jni, socketImplClass, "getFileDescriptor", "()Ljava/io/FileDescriptor;");

    jclass fdClass                 = (*jni)->FindClass(jni, "java/io/FileDescriptor");
    g_java.fid_FileDescriptor_fd   = (*jni)->GetFieldID(jni, fdClass, "fd", "I");

    jclass inetAddressClass        = (*jni)->FindClass(jni, "java/net/InetAddress");
    g_java.mid_InetAddress_getHostAddress = (*jni)->GetMethodID(jni, inetAddressClass, "getHostAddress", "()Ljava/lang/String;");
}

static void 
initAppOutputStreamGlobals(JNIEnv *jni)
{
    if (g_java.mid_AppOutputStream___write != NULL) return;
    jclass appOutputStreamClass = (*jni)->FindClass(jni, "sun/security/ssl/AppOutputStream");
    if (appOutputStreamClass == NULL) {
        // Oracle JDK 6
        appOutputStreamClass = (*jni)->FindClass(jni, "com/sun/net/ssl/internal/ssl/AppOutputStream");
    }
    if (appOutputStreamClass == NULL) {
        // JDK 11
        appOutputStreamClass = (*jni)->FindClass(jni, "sun/security/ssl/SSLSocketImpl$AppOutputStream");
    }
    g_java.mid_AppOutputStream___write = (*jni)->GetMethodID(jni, appOutputStreamClass, "__write", "([BII)V");
    /*
    We are trying to find a private field of type SSLSocketImpl which holds a reference to the socket object.
    - in JDK 6-8 the field is called "c"
    - in JDK 9-10 the field is called "socket"
    - in JDK 11-14 AppOutputStream is a nested private class inside the SSLSocketImpl class, 
      so we need to find a reference to the instance of its enclosing class which is an implicit field called "this$0"
    */
    g_java.fid_AppOutputStream_socket = (*jni)->GetFieldID(jni, appOutputStreamClass, "c", "Lsun/security/ssl/SSLSocketImpl;");
    if (g_java.fid_AppOutputStream_socket == NULL) {
        //support for Oracle JDK 6
        g_java.fid_AppOutputStream_socket = (*jni)->GetFieldID(jni, appOutputStreamClass, "c", "Lcom/sun/net/ssl/internal/ssl/SSLSocketImpl;");
    }
    if (g_java.fid_AppOutputStream_socket == NULL) {
        //support for JDK 9, JDK 10
        g_java.fid_AppOutputStream_socket = (*jni)->GetFieldID(jni, appOutputStreamClass, "socket", "Lsun/security/ssl/SSLSocketImpl;");
    }
    if (g_java.fid_AppOutputStream_socket == NULL) {
        //support for JDK 11 - 14
        g_java.fid_AppOutputStream_socket = (*jni)->GetFieldID(jni, appOutputStreamClass, "this$0", "Lsun/security/ssl/SSLSocketImpl;");
    }
    if (g_java.fid_AppOutputStream_socket == NULL) {
        scopeLog("unable to find an SSLSocket field in AppOutputStream class", -1, CFG_LOG_DEBUG);
    }
    clearJniException(jni);
}

static void
initAppInputStreamGlobals(JNIEnv *jni)
{
    if (g_java.mid_AppInputStream___read != NULL) return;
    jclass appInputStreamClass = (*jni)->FindClass(jni, "sun/security/ssl/AppInputStream");
    if (appInputStreamClass == NULL) {
        // Oracle JDK 6
        appInputStreamClass  = (*jni)->FindClass(jni, "com/sun/net/ssl/internal/ssl/AppInputStream");
    }
    if (appInputStreamClass == NULL) {
        // JDK 11
        appInputStreamClass  = (*jni)->FindClass(jni, "sun/security/ssl/SSLSocketImpl$AppInputStream");
    }
    g_java.mid_AppInputStream___read = (*jni)->GetMethodID(jni, appInputStreamClass, "__read", "([BII)I");
    /*
    We are trying to find a private field of type SSLSocketImpl which holds a reference to the socket object.
    - in JDK 6-8 the field is called "c"
    - in JDK 9-10 the field is called "socket"
    - in JDK 11-14 AppInputStream is a nested private class inside the SSLSocketImpl class, 
      so we need to find a reference to the instance of its enclosing class which is an implicit field called "this$0"
    */
    g_java.fid_AppInputStream_socket = (*jni)->GetFieldID(jni, appInputStreamClass, "c", "Lsun/security/ssl/SSLSocketImpl;");
    if (g_java.fid_AppInputStream_socket == NULL) {
        //support for Oracle JDK 6
        g_java.fid_AppInputStream_socket = (*jni)->GetFieldID(jni, appInputStreamClass, "c", "Lcom/sun/net/ssl/internal/ssl/SSLSocketImpl;");
    }
    if (g_java.fid_AppInputStream_socket == NULL) {
        //support for JDK 9, JDK 10
        g_java.fid_AppInputStream_socket = (*jni)->GetFieldID(jni, appInputStreamClass, "socket", "Lsun/security/ssl/SSLSocketImpl;");
    }
    if (g_java.fid_AppInputStream_socket == NULL) {
        //support for JDK 11 - 14
        g_java.fid_AppInputStream_socket = (*jni)->GetFieldID(jni, appInputStreamClass, "this$0", "Lsun/security/ssl/SSLSocketImpl;");
    }
    if (g_java.fid_AppInputStream_socket == NULL) {
        scopeLog("unable to find an SSLSocket field in AppInputStream class", -1, CFG_LOG_DEBUG);
    }
    clearJniException(jni);
}

static void
initSSLEngineImplGlobals(JNIEnv *jni) 
{
    if (g_java.mid_SSLEngineImpl___unwrap != NULL) return;
    jclass sslEngineImplClass = (*jni)->FindClass(jni, "sun/security/ssl/SSLEngineImpl");
    if (sslEngineImplClass == NULL) {
        // Oracle JDK 6
        sslEngineImplClass  = (*jni)->FindClass(jni, "com/sun/net/ssl/internal/ssl/SSLEngineImpl");
        clearJniException(jni);
    }
    g_java.mid_SSLEngineImpl___unwrap    = (*jni)->GetMethodID(jni, sslEngineImplClass, "__unwrap", "(Ljava/nio/ByteBuffer;[Ljava/nio/ByteBuffer;II)Ljavax/net/ssl/SSLEngineResult;");
    g_java.mid_SSLEngineImpl___wrap      = (*jni)->GetMethodID(jni, sslEngineImplClass, "__wrap", "([Ljava/nio/ByteBuffer;IILjava/nio/ByteBuffer;)Ljavax/net/ssl/SSLEngineResult;");
    g_java.mid_SSLEngineImpl_getSession  = (*jni)->GetMethodID(jni, sslEngineImplClass, "getSession", "()Ljavax/net/ssl/SSLSession;");

    jclass socketChannelClass = (*jni)->FindClass(jni, "sun/nio/ch/SocketChannelImpl");
    g_java.mid_SocketChannelImpl___read  = (*jni)->GetMethodID(jni, socketChannelClass, "__read", "(Ljava/nio/ByteBuffer;)I");
    g_java.mid_SocketChannelImpl___write = (*jni)->GetMethodID(jni, socketChannelClass, "__write", "(Ljava/nio/ByteBuffer;)I");
    g_java.mid_SocketChannelImpl_getRemoteAddress  = (*jni)->GetMethodID(jni, socketChannelClass, "getRemoteAddress", "()Ljava/net/SocketAddress;");
    if (g_java.mid_SocketChannelImpl_getRemoteAddress == NULL) {
        // Open JDK 6
        g_java.mid_SocketChannelImpl_getRemoteAddress  = (*jni)->GetMethodID(jni, socketChannelClass, "remoteAddress", "()Ljava/net/SocketAddress;");
        clearJniException(jni);
    }
    g_java.mid_SocketChannelImpl_getFDVal  = (*jni)->GetMethodID(jni, socketChannelClass, "getFDVal", "()I");

    jclass byteBufferClass         = (*jni)->FindClass(jni, "java/nio/ByteBuffer");
    jclass bufferClass             = (*jni)->FindClass(jni, "java/nio/Buffer");
    g_java.mid_ByteBuffer_array    = (*jni)->GetMethodID(jni, byteBufferClass, "array", "()[B");
    g_java.mid_ByteBuffer_position = (*jni)->GetMethodID(jni, bufferClass, "position", "()I");
    g_java.mid_ByteBuffer_limit    = (*jni)->GetMethodID(jni, bufferClass, "limit", "()I");

    jclass dbbClass                = (*jni)->FindClass(jni, "java/nio/DirectByteBuffer");
    g_java.fid_ByteBuffer___fd     = (*jni)->GetFieldID(jni, dbbClass, "__fd", "I");
    if (g_java.fid_ByteBuffer___fd == NULL) {
        // Open JDK 9
        dbbClass                   = (*jni)->FindClass(jni, "java/nio/DirectByteBufferR");
        g_java.fid_ByteBuffer___fd = (*jni)->GetFieldID(jni, dbbClass, "__fd", "I");
        clearJniException(jni);
    }
}

void JNICALL 
ClassFileLoadHook(jvmtiEnv *jvmti_env,
    JNIEnv* jni,
    jclass class_being_redefined,
    jobject loader,
    const char* name,
    jobject protection_domain,
    jint class_data_len,
    const unsigned char* class_data,
    jint* new_class_data_len,
    unsigned char** new_class_data) 
{
    if (name == NULL) return;
    
    if (strcmp(name, "sun/security/ssl/AppOutputStream") == 0 || 
        strcmp(name, "com/sun/net/ssl/internal/ssl/AppOutputStream") == 0 ||
        strcmp(name, "sun/security/ssl/SSLSocketImpl$AppOutputStream") == 0) {

        scopeLog("installing Java SSL hooks for AppOutputStream class...", -1, CFG_LOG_INFO);

        java_class_t *classInfo = javaReadClass(class_data);

        int methodIndex = javaFindMethodIndex(classInfo, "write", "([BII)V");
        if (methodIndex == -1) {
            javaDestroy(&classInfo);
            scopeLog("ERROR: 'write' method not found in AppOutputStream class\n", -1, CFG_LOG_ERROR);
            return;
        }
        javaCopyMethod(classInfo, classInfo->methods[methodIndex], "__write");
        javaConvertMethodToNative(classInfo, methodIndex);

        unsigned char *dest;
        (*jvmti_env)->Allocate(jvmti_env, classInfo->length, &dest);
        javaWriteClass(dest, classInfo);

        *new_class_data_len = classInfo->length;
        *new_class_data = dest;
        javaDestroy(&classInfo);
    }

    if (strcmp(name, "sun/security/ssl/AppInputStream") == 0 ||
        strcmp(name, "com/sun/net/ssl/internal/ssl/AppInputStream") == 0 ||
        strcmp(name, "sun/security/ssl/SSLSocketImpl$AppInputStream") == 0) {

        scopeLog("installing Java SSL hooks for AppInputStream class...", -1, CFG_LOG_INFO);

        java_class_t *classInfo = javaReadClass(class_data);

        int methodIndex = javaFindMethodIndex(classInfo, "read", "([BII)I");
        if (methodIndex == -1) {
            javaDestroy(&classInfo);
            scopeLog("ERROR: 'read' method not found in AppInputStream class\n", -1, CFG_LOG_ERROR);
            return;
        }
        javaCopyMethod(classInfo, classInfo->methods[methodIndex], "__read");
        javaConvertMethodToNative(classInfo, methodIndex);

        unsigned char *dest;
        (*jvmti_env)->Allocate(jvmti_env, classInfo->length, &dest);
        javaWriteClass(dest, classInfo);

        *new_class_data_len = classInfo->length;
        *new_class_data = dest;
        javaDestroy(&classInfo);
    }

    if (strcmp(name, "sun/security/ssl/SSLEngineImpl") == 0 || 
        strcmp(name, "com/sun/net/ssl/internal/ssl/SSLEngineImpl") == 0) {
        
        scopeLog("installing Java SSL hooks for SSLEngineImpl class...", -1, CFG_LOG_INFO);

        java_class_t *classInfo = javaReadClass(class_data);

        int methodIndex = javaFindMethodIndex(classInfo, "wrap", "([Ljava/nio/ByteBuffer;IILjava/nio/ByteBuffer;)Ljavax/net/ssl/SSLEngineResult;");
        if (methodIndex == -1) {
            javaDestroy(&classInfo);
            scopeLog("ERROR: 'wrap' method not found in SSLEngineImpl class\n", -1, CFG_LOG_ERROR);
            return;
        }
        javaCopyMethod(classInfo, classInfo->methods[methodIndex], "__wrap");
        javaConvertMethodToNative(classInfo, methodIndex);

        methodIndex = javaFindMethodIndex(classInfo, "unwrap", "(Ljava/nio/ByteBuffer;[Ljava/nio/ByteBuffer;II)Ljavax/net/ssl/SSLEngineResult;");
         if (methodIndex == -1) {
            javaDestroy(&classInfo);
            scopeLog("ERROR: 'unwrap' method not found in SSLEngineImpl class\n", -1, CFG_LOG_ERROR);
            return;
        }
        javaCopyMethod(classInfo, classInfo->methods[methodIndex], "__unwrap");
        javaConvertMethodToNative(classInfo, methodIndex);
        
        unsigned char *dest;
        (*jvmti_env)->Allocate(jvmti_env, classInfo->length, &dest);
        javaWriteClass(dest, classInfo);

        *new_class_data_len = classInfo->length;
        *new_class_data = dest;
        javaDestroy(&classInfo);
    }

    if (strcmp(name, "sun/nio/ch/SocketChannelImpl") == 0) {

        scopeLog("installing Java SSL hooks for SocketChannelImpl class...", -1, CFG_LOG_INFO);
        java_class_t *classInfo = javaReadClass(class_data);

        int methodIndex = javaFindMethodIndex(classInfo, "read", "(Ljava/nio/ByteBuffer;)I");
        if (methodIndex == -1) {
            javaDestroy(&classInfo);
            scopeLog("ERROR: 'read' method not found in SocketChannelImpl class\n", -1, CFG_LOG_ERROR);
            return;
        }
        javaCopyMethod(classInfo, classInfo->methods[methodIndex], "__read");
        javaConvertMethodToNative(classInfo, methodIndex);

        methodIndex = javaFindMethodIndex(classInfo, "write", "(Ljava/nio/ByteBuffer;)I");
        if (methodIndex == -1) {
            javaDestroy(&classInfo);
            scopeLog("ERROR: 'write' method not found in SocketChannelImpl class\n", -1, CFG_LOG_ERROR);
            return;
        }
        javaCopyMethod(classInfo, classInfo->methods[methodIndex], "__write");
        javaConvertMethodToNative(classInfo, methodIndex);

        unsigned char *dest;
        (*jvmti_env)->Allocate(jvmti_env, classInfo->length, &dest);
        javaWriteClass(dest, classInfo);

        *new_class_data_len = classInfo->length;
        *new_class_data = dest;
        javaDestroy(&classInfo);
    }

    if (strcmp(name, "java/nio/DirectByteBuffer") == 0 ||
        strcmp(name, "java/nio/DirectByteBufferR") == 0) {

        scopeLog("installing Java SSL hooks for java.nio.DirectByteBuffer class...", -1, CFG_LOG_INFO);
        java_class_t *classInfo = javaReadClass(class_data);

        // add a private field which will hold the fd used to read/write data for that buffer
        javaAddField(classInfo, "__fd", "I", ACC_PRIVATE);

        unsigned char *dest;
        (*jvmti_env)->Allocate(jvmti_env, classInfo->length, &dest);
        javaWriteClass(dest, classInfo);

        *new_class_data_len = classInfo->length;
        *new_class_data = dest;
        javaDestroy(&classInfo);
    }
}

static void 
doJavaProtocol(JNIEnv *jni, jobject session, jbyteArray buf, jint offset, jint len, metric_t src, int fd)
{
    jint  hash      = (*jni)->CallIntMethod(jni, session, g_java.mid_Object_hashCode);
    jbyte *byteBuf  = (*jni)->GetPrimitiveArrayCritical(jni, buf, 0);
    doProtocol((uint64_t)hash, fd, &byteBuf[offset], (size_t)(len - offset), src, BUF);
    (*jni)->ReleasePrimitiveArrayCritical(jni, buf, buf, 0);
}

static void
saveSocketChannel(JNIEnv *jni, jobject socketChannel, jobject buf)
{
    jint fd = (*jni)->CallIntMethod(jni, socketChannel, g_java.mid_SocketChannelImpl_getFDVal);
    //store the file descriptor in the internal byte buffer's field
    (*jni)->SetIntField(jni, buf, g_java.fid_ByteBuffer___fd, fd);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_SocketChannelImpl_read(JNIEnv *jni, jobject obj, jobject buf)
{
    initJniGlobals(jni);
    initSSLEngineImplGlobals(jni);
    
    saveSocketChannel(jni, obj, buf);
    
    jint res = (*jni)->CallIntMethod(jni, obj, g_java.mid_SocketChannelImpl___read, buf);
    return res;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_SocketChannelImpl_write(JNIEnv *jni, jobject obj, jobject buf)
{
    initJniGlobals(jni);
    initSSLEngineImplGlobals(jni);
    
    saveSocketChannel(jni, obj, buf);
    
    jint res = (*jni)->CallIntMethod(jni, obj, g_java.mid_SocketChannelImpl___write, buf);
    return res;
}

JNIEXPORT jobject JNICALL
Java_sun_security_ssl_SSLEngineImpl_unwrap(JNIEnv *jni, jobject obj, jobject src, jobjectArray dsts, jint offset, jint len)
{
    int fd = -1;

    initJniGlobals(jni);
    initSSLEngineImplGlobals(jni);

    jint fdVal = (uint64_t) (*jni)->GetIntField(jni, src, g_java.fid_ByteBuffer___fd);
    if (fdVal) {
        fd = fdVal;
    }

    //call the original method
    jobject res = (*jni)->CallObjectMethod(jni, obj, g_java.mid_SSLEngineImpl___unwrap, src, dsts, offset, len);

    jobject session = (*jni)->CallObjectMethod(jni, obj, g_java.mid_SSLEngineImpl_getSession);
    int i;
    for(i=offset;i<len - offset;i++) {
        jobject bufEl  = (*jni)->GetObjectArrayElement(jni, dsts, i);
        jint pos       = (*jni)->CallIntMethod(jni, bufEl, g_java.mid_ByteBuffer_position);
        jbyteArray buf = (*jni)->CallObjectMethod(jni, bufEl, g_java.mid_ByteBuffer_array);
        doJavaProtocol(jni, session, buf, 0, pos, TLSRX, fd);
    }
    return res;
}

JNIEXPORT jobject JNICALL 
Java_com_sun_net_ssl_internal_ssl_SSLEngineImpl_unwrap(JNIEnv *jni, jobject obj, jobject netData, jobjectArray appData, jint offset, jint len) 
{
    return Java_sun_security_ssl_SSLEngineImpl_unwrap(jni, obj, netData, appData, offset, len);
}

JNIEXPORT jobject JNICALL 
Java_sun_security_ssl_SSLEngineImpl_wrap(JNIEnv *jni, jobject obj, jobjectArray srcs, jint offset, jint len, jobject dst) 
{
    int fd = -1;

    initJniGlobals(jni);
    initSSLEngineImplGlobals(jni);

    jint fdVal = (uint64_t) (*jni)->GetIntField(jni, dst, g_java.fid_ByteBuffer___fd);
    if (fdVal) {
        fd = fdVal;
    }
    
    jobject session = (*jni)->CallObjectMethod(jni, obj, g_java.mid_SSLEngineImpl_getSession);
    int i;
    for(i=offset;i<len - offset;i++) {
        jobject bufEl  = (*jni)->GetObjectArrayElement(jni, srcs, i);
        jint pos       = (*jni)->CallIntMethod(jni, bufEl, g_java.mid_ByteBuffer_position);
        jint limit     = (*jni)->CallIntMethod(jni, bufEl, g_java.mid_ByteBuffer_limit);
        jbyteArray buf = (*jni)->CallObjectMethod(jni, bufEl, g_java.mid_ByteBuffer_array);
        doJavaProtocol(jni, session, buf, pos, limit, TLSTX, fd);
    }
    
    //call the original method
    jobject res = (*jni)->CallObjectMethod(jni, obj, g_java.mid_SSLEngineImpl___wrap, srcs, offset, len, dst);
    return res;
}

JNIEXPORT jobject JNICALL 
Java_com_sun_net_ssl_internal_ssl_SSLEngineImpl_wrap(JNIEnv *jni, jobject obj, jobjectArray appData, jint offset, jint len, jobject netData) 
{
    return Java_sun_security_ssl_SSLEngineImpl_wrap(jni, obj, appData, offset, len, netData);
}

JNIEXPORT void JNICALL 
Java_sun_security_ssl_AppOutputStream_write(JNIEnv *jni, jobject obj, jbyteArray buf, jint offset, jint len) 
{
    int fd = -1;

    initJniGlobals(jni);
    initAppOutputStreamGlobals(jni);

    jobject session;
    if (g_java.fid_AppOutputStream_socket != NULL) {
        jobject socket  = (*jni)->GetObjectField(jni, obj, g_java.fid_AppOutputStream_socket);
        session = (*jni)->CallObjectMethod(jni, socket, g_java.mid_SSLSocketImpl_getSession);
        fd = getFdFromSocket(jni, socket);
    } else {
        session = obj;
    }
    
    doJavaProtocol(jni, session, buf, offset, len, TLSTX, fd);
    
    //call the original method
    (*jni)->CallVoidMethod(jni, obj, g_java.mid_AppOutputStream___write, buf, offset, len);
}

 //support for JDK 11 - 14 where AppOutputStream in a nested class defined inside SSLSocketImpl
JNIEXPORT void JNICALL 
Java_sun_security_ssl_SSLSocketImpl_00024AppOutputStream_write(JNIEnv *jni, jobject obj, jbyteArray buf, jint offset, jint len) 
{
    Java_sun_security_ssl_AppOutputStream_write(jni, obj, buf, offset, len);
}

JNIEXPORT void JNICALL 
Java_com_sun_net_ssl_internal_ssl_AppOutputStream_write(JNIEnv *jni, jobject obj, jbyteArray buf, jint offset, jint len) 
{
    Java_sun_security_ssl_AppOutputStream_write(jni, obj, buf, offset, len);
}

JNIEXPORT jint JNICALL 
Java_sun_security_ssl_AppInputStream_read(JNIEnv *jni, jobject obj, jbyteArray buf, jint offset, jint len) 
{
    int fd = -1;

    initJniGlobals(jni);
    initAppInputStreamGlobals(jni);

    jobject session;
    if (g_java.fid_AppInputStream_socket != NULL) {
        jobject socket  = (*jni)->GetObjectField(jni, obj, g_java.fid_AppInputStream_socket);
        session = (*jni)->CallObjectMethod(jni, socket, g_java.mid_SSLSocketImpl_getSession);
        fd = getFdFromSocket(jni, socket);
    } else {
        session = obj;
    }

    //call the original method
    jint res = (*jni)->CallIntMethod(jni, obj, g_java.mid_AppInputStream___read, buf, offset, len);

    doJavaProtocol(jni, session, buf, offset, res, TLSRX, fd);

    return res;
}

//support for JDK 11 - 14 where AppInputStream in a nested class defined inside SSLSocketImpl
JNIEXPORT jint JNICALL 
Java_sun_security_ssl_SSLSocketImpl_00024AppInputStream_read(JNIEnv *jni, jobject obj, jbyteArray buf, jint offset, jint len) 
{
    return Java_sun_security_ssl_AppInputStream_read(jni, obj, buf, offset, len);
}

JNIEXPORT jint JNICALL 
Java_com_sun_net_ssl_internal_ssl_AppInputStream_read(JNIEnv *jni, jobject obj, jbyteArray buf, jint offset, jint len) 
{
    return Java_sun_security_ssl_AppInputStream_read(jni, obj, buf, offset, len);
}

JNIEXPORT jint JNICALL 
Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) 
{
    jvmtiError error;
    jvmtiEnv *env;

    scopeLog("Initializing Java agent", -1, CFG_LOG_INFO);

    jint result = (*jvm)->GetEnv(jvm, (void **) &env, JVMTI_VERSION_1_0);
    if (result != 0) {
        scopeLog("ERROR: GetEnv failed\n", -1, CFG_LOG_ERROR);
        return JNI_ERR;
    }

    jvmtiCapabilities capabilities;
    memset(&capabilities,0, sizeof(capabilities));

    capabilities.can_generate_all_class_hook_events = 1;
    error = (*env)->AddCapabilities(env, &capabilities);
    if (error != JVMTI_ERROR_NONE) {
        logJvmtiError(env, error, "AddCapabilities");
        return JNI_ERR;
    }

    error = (*env)->SetEventNotificationMode(env, JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
    if (error != JVMTI_ERROR_NONE) {
        logJvmtiError(env, error, "SetEventNotificationMode");
        return JNI_ERR;
    }
   
    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ClassFileLoadHook = &ClassFileLoadHook;
    error = (*env)->SetEventCallbacks(env, &callbacks, sizeof(callbacks));
    if (error != JVMTI_ERROR_NONE) {
        logJvmtiError(env, error, "SetEventCallbacks");
        return JNI_ERR;
    }

    return JNI_OK;
}

// This overrides a weak definition in src/linux/os.c
void
initJavaAgent() {
    char *var = getenv("LD_PRELOAD");
    if (var != NULL) {
        /*
        set JAVA_TOOL_OPTIONS so that JVM can load libscope.so as a java agent
        https://docs.oracle.com/javase/8/docs/platform/jvmti/jvmti.html#tooloptions
        */
        char opt[1024];
        snprintf(opt, sizeof(opt), "-agentpath:%s", var);

        char *buf;
        size_t bufsize = strlen(opt) + 1;

        char *env = getenv("JAVA_TOOL_OPTIONS");
        if (env != NULL) {
            if (strstr(env, opt) != NULL) {
                //agentpath is already set, do nothing
                return;
            }
            bufsize += strlen(env) + 1;
        }
        buf = malloc(bufsize);
        snprintf(buf, bufsize, "%s%s%s", env != NULL ? env : "", env != NULL ? " " : "", opt);

        int result = setenv("JAVA_TOOL_OPTIONS", buf, 1);
        if (result) {
            scopeLog("ERROR: Could not set JAVA_TOOL_OPTIONS failed\n", -1, CFG_LOG_ERROR);
        }
        free(buf);
    }
}
