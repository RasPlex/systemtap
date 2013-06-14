#include "HelperSDT.h"
#include "jni.h"
#include <sys/sdt.h>
#include <stdbool.h>
#include "stdlib.h"
#include "string.h"

typedef enum { NONE, INTEGER, BYTE, BOOL, CHAR, SHORT, LONG, DOUBLE, FLOAT, ARRAY, OTHER, STRING } Type;

typedef struct {

  Type type;
  union {
    int i;
    char b; //we're actually using this as a byte, this is how its handled in jni.h
    bool bl;
    char* c;
    int ch;
    short s;
    long long l;
    long long d;
    long long f;
    bool error;
    int *ai;
    double *ad;
    bool *abl;
  } vartype;
} _staparg;

char* get_java_string(JNIEnv *env, jobject _string)
{
  const char* __string = (*env)->GetStringUTFChars(env, _string, NULL);
  (*env)->ReleaseStringUTFChars(env, _string, NULL);
  char* string = malloc(strlen( __string)+1);
  strcpy(string, __string);
  return string;
}

_staparg determine_java_type(JNIEnv *env, jobject _arg, _staparg staparg)
{
  jclass class_arg = (*env)->GetObjectClass(env, _arg);
  jfieldID fidNumber = 0;
  fidNumber = (*env)->GetFieldID(env, class_arg, "value", "I");
  if (NULL == fidNumber)
    {
      (*env)->ExceptionClear(env);
      fidNumber = 0;
    }
  else
    {
      staparg.type = INTEGER;
      staparg.vartype.i = (*env)->GetIntField(env, _arg, fidNumber);
      return staparg;
    }
  fidNumber = (*env)->GetFieldID(env, class_arg, "value", "B");
  if (NULL == fidNumber)
    {
      (*env)->ExceptionClear(env);
      fidNumber = 0;
    }
  else
    {
      staparg.type = BYTE;
      staparg.vartype.b = (*env)->GetByteField(env, _arg, fidNumber);
      return staparg;
    }
  fidNumber = (*env)->GetFieldID(env, class_arg, "value", "Z");
  if (NULL == fidNumber)
    {
      (*env)->ExceptionClear(env);
      fidNumber = 0;
    }
  else
    {
      staparg.type = BOOL;
      staparg.vartype.bl = (*env)->GetBooleanField(env, _arg, fidNumber);
      return staparg;
    }
  fidNumber = (*env)->GetFieldID(env, class_arg, "value", "C");
  if (NULL == fidNumber)
    {
      (*env)->ExceptionClear(env);
      fidNumber = 0;
    }
  else
    {
      staparg.type = CHAR;
      staparg.vartype.ch = (*env)->GetCharField(env, _arg, fidNumber);
      return staparg;
    }
  fidNumber = (*env)->GetFieldID(env, class_arg, "value", "S");
  if (NULL == fidNumber)
    {
      (*env)->ExceptionClear(env);
      fidNumber = 0;
    }
  else
    {
      staparg.type = SHORT;
      staparg.vartype.s = (*env)->GetShortField(env, _arg, fidNumber);
      return staparg;
    }
  fidNumber = (*env)->GetFieldID(env, class_arg, "value", "J");
  if (NULL == fidNumber)
    {
      (*env)->ExceptionClear(env);
      fidNumber = 0;
    }
  else
    {
      staparg.type = LONG;
      staparg.vartype.l = (*env)->GetLongField(env, _arg, fidNumber);
      return staparg;
    }
  fidNumber = (*env)->GetFieldID(env, class_arg, "value", "F");
  if (NULL == fidNumber)
    {
      (*env)->ExceptionClear(env);
      fidNumber = 0;
    }
  else
    {
      staparg.type = FLOAT;
      staparg.vartype.f = (*env)->GetFloatField(env, _arg, fidNumber);
      return staparg;
    }
  fidNumber = (*env)->GetFieldID(env, class_arg, "value", "D");
  if (NULL == fidNumber)
    {
      (*env)->ExceptionClear(env);
      fidNumber = 0;
    }
  else
    {
      staparg.type = DOUBLE;
      staparg.vartype.d = (*env)->GetDoubleField(env, _arg, fidNumber);
      return staparg;
    }

  staparg.type = OTHER;
  return staparg;
}
/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE0
 * Signature: (Ljava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE0
(JNIEnv *env, jobject obj, jstring _rulename)
{
  char* rulename = get_java_string(env, _rulename);
  STAP_PROBE1(HelperSDT, method__0, rulename);
}

/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE1
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE1
(JNIEnv *env, jobject obj, jstring _rulename, jobject _arg1)
{
  char* rulename = get_java_string(env, _rulename);
  _staparg arg1 = {0}; //initialize to zero so we don't get garbage the first time through
  arg1 = determine_java_type(env, _arg1, arg1);
  if(arg1.type == OTHER || arg1.type == NONE)
    arg1.vartype.c = get_java_string(env, _arg1); // we need to create some type of check for strings
  STAP_PROBE2(HelperSDT, method__1, arg1.vartype.d, rulename);
}

/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE2
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE2
(JNIEnv *env, jobject obj, jstring _rulename, jobject _arg1, jobject _arg2)
{
  char* rulename = get_java_string(env, _rulename);
  _staparg arg1 = {0}; //initialize to zero so we don't get garbage the first time through
  _staparg arg2 = {0};
  arg1 = determine_java_type(env, _arg1, arg1);
  arg2 = determine_java_type(env, _arg2, arg2);
  if(arg1.type == OTHER || arg1.type == NONE)
    arg1.vartype.c = get_java_string(env, _arg1); // we need to create some type of check for strings
  if(arg2.type == OTHER || arg2.type == NONE)
    arg2.vartype.c = get_java_string(env, _arg2);
  STAP_PROBE3(HelperSDT, method__2, arg1.vartype.d, arg2.vartype.d, rulename);

}

/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE3
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE3
(JNIEnv *env, jobject obj, jstring _rulename, jobject _arg1, jobject _arg2, jobject _arg3)
{
  char* rulename = get_java_string(env, _rulename);
  _staparg arg1 = {0}; //initialize to zero so we don't get garbage the first time through
  _staparg arg2 = {0};
  _staparg arg3 = {0};
  arg1 = determine_java_type(env, _arg1, arg1);
  arg2 = determine_java_type(env, _arg2, arg2);
  arg3 = determine_java_type(env, _arg3, arg3);
  if(arg1.type == OTHER || arg1.type == NONE)
    arg1.vartype.c = get_java_string(env, _arg1); // we need to create some type of check for strings
  if(arg2.type == OTHER || arg2.type == NONE)
    arg2.vartype.c = get_java_string(env, _arg2);
  if(arg3.type == OTHER || arg3.type == NONE)
    arg3.vartype.c = get_java_string(env, _arg3);
  STAP_PROBE4(HelperSDT, method__3, arg1.vartype.d, arg2.vartype.d, arg3.vartype.d, rulename);
}

/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE4
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE4
(JNIEnv *env, jobject obj, jstring _rulename, jobject _arg1, jobject _arg2, jobject _arg3, jobject _arg4)
{
  char* rulename = get_java_string(env, _rulename);
  _staparg arg1 = {0}; //initialize to zero so we don't get garbage the first time through
  _staparg arg2 = {0};
  _staparg arg3 = {0};
  _staparg arg4 = {0};
  arg1 = determine_java_type(env, _arg1, arg1);
  arg2 = determine_java_type(env, _arg2, arg2);
  arg3 = determine_java_type(env, _arg3, arg3);
  arg4 = determine_java_type(env, _arg4, arg4);
  if(arg1.type == OTHER || arg1.type == NONE)
    arg1.vartype.c = get_java_string(env, _arg1); // we need to create some type of check for strings
  if(arg2.type == OTHER || arg2.type == NONE)
    arg2.vartype.c = get_java_string(env, _arg2);
  if(arg3.type == OTHER || arg3.type == NONE)
    arg3.vartype.c = get_java_string(env, _arg3);
  if(arg4.type == OTHER || arg4.type == NONE)
    arg4.vartype.c = get_java_string(env, _arg4);
  STAP_PROBE5(HelperSDT, method__4, arg1.vartype.d, arg2.vartype.d, arg3.vartype.d, arg4.vartype.d, rulename);
}

/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE5
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE5
(JNIEnv *env, jobject obj, jstring _rulename, jobject _arg1, jobject _arg2, jobject _arg3, jobject _arg4, jobject _arg5)
{
  char* rulename = get_java_string(env, _rulename);
  _staparg arg1 = {0}; //initialize to zero so we don't get garbage the first time through
  _staparg arg2 = {0};
  _staparg arg3 = {0};
  _staparg arg4 = {0};
  _staparg arg5 = {0};
  arg1 = determine_java_type(env, _arg1, arg1);
  arg2 = determine_java_type(env, _arg2, arg2);
  arg3 = determine_java_type(env, _arg3, arg3);
  arg4 = determine_java_type(env, _arg4, arg4);
  arg5 = determine_java_type(env, _arg5, arg5);
  if(arg1.type == OTHER || arg1.type == NONE)
    arg1.vartype.c = get_java_string(env, _arg1); // we need to create some type of check for strings
  if(arg2.type == OTHER || arg2.type == NONE)
    arg2.vartype.c = get_java_string(env, _arg2);
  if(arg3.type == OTHER || arg3.type == NONE)
    arg3.vartype.c = get_java_string(env, _arg3);
  if(arg4.type == OTHER || arg4.type == NONE)
    arg4.vartype.c = get_java_string(env, _arg4);
  if(arg5.type == OTHER || arg5.type == NONE)
    arg5.vartype.c = get_java_string(env, _arg5);
  STAP_PROBE6(HelperSDT, method__5, arg1.vartype.d, arg2.vartype.d, arg3.vartype.d, arg4.vartype.d, arg5.vartype.d, rulename);
}

/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE6
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE6
(JNIEnv *env, jobject obj, jstring _rulename, jobject _arg1, jobject _arg2, jobject _arg3, jobject _arg4, jobject _arg5, jobject _arg6)
{
  char* rulename = get_java_string(env, _rulename);
  _staparg arg1 = {0}; //initialize to zero so we don't get garbage the first time through
  _staparg arg2 = {0};
  _staparg arg3 = {0};
  _staparg arg4 = {0};
  _staparg arg5 = {0};
  _staparg arg6 = {0};
  arg1 = determine_java_type(env, _arg1, arg1);
  arg2 = determine_java_type(env, _arg2, arg2);
  arg3 = determine_java_type(env, _arg3, arg3);
  arg4 = determine_java_type(env, _arg4, arg4);
  arg5 = determine_java_type(env, _arg5, arg5);
  arg6 = determine_java_type(env, _arg6, arg6);
  if(arg1.type == OTHER || arg1.type == NONE)
    arg1.vartype.c = get_java_string(env, _arg1); // we need to create some type of check for strings
  if(arg2.type == OTHER || arg2.type == NONE)
    arg2.vartype.c = get_java_string(env, _arg2);
  if(arg3.type == OTHER || arg3.type == NONE)
    arg3.vartype.c = get_java_string(env, _arg3);
  if(arg4.type == OTHER || arg4.type == NONE)
    arg4.vartype.c = get_java_string(env, _arg4);
  if(arg5.type == OTHER || arg5.type == NONE)
    arg5.vartype.c = get_java_string(env, _arg5);
  if(arg6.type == OTHER || arg6.type == NONE)
    arg6.vartype.c = get_java_string(env, _arg6);
  STAP_PROBE7(HelperSDT, method__6, arg1.vartype.d, arg2.vartype.d, arg3.vartype.d, arg4.vartype.d, arg5.vartype.d, arg6.vartype.d, rulename);
}

/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE7
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE7
(JNIEnv *env, jobject obj, jstring _rulename, jobject _arg1, jobject _arg2, jobject _arg3, jobject _arg4, jobject _arg5, jobject _arg6, jobject _arg7)
{
  char* rulename = get_java_string(env, _rulename);
  _staparg arg1 = {0}; //initialize to zero so we don't get garbage the first time through
  _staparg arg2 = {0};
  _staparg arg3 = {0};
  _staparg arg4 = {0};
  _staparg arg5 = {0};
  _staparg arg6 = {0};
  _staparg arg7 = {0};
  arg1 = determine_java_type(env, _arg1, arg1);
  arg2 = determine_java_type(env, _arg2, arg2);
  arg3 = determine_java_type(env, _arg3, arg3);
  arg4 = determine_java_type(env, _arg4, arg4);
  arg5 = determine_java_type(env, _arg5, arg5);
  arg6 = determine_java_type(env, _arg6, arg6);
  arg7 = determine_java_type(env, _arg7, arg7);
  if(arg1.type == OTHER || arg1.type == NONE)
    arg1.vartype.c = get_java_string(env, _arg1); // we need to create some type of check for strings
  if(arg2.type == OTHER || arg2.type == NONE)
    arg2.vartype.c = get_java_string(env, _arg2);
  if(arg3.type == OTHER || arg3.type == NONE)
    arg3.vartype.c = get_java_string(env, _arg3);
  if(arg4.type == OTHER || arg4.type == NONE)
    arg4.vartype.c = get_java_string(env, _arg4);
  if(arg5.type == OTHER || arg5.type == NONE)
    arg5.vartype.c = get_java_string(env, _arg5);
  if(arg6.type == OTHER || arg6.type == NONE)
    arg6.vartype.c = get_java_string(env, _arg6);
  if(arg7.type == OTHER || arg7.type == NONE)
    arg7.vartype.c = get_java_string(env, _arg7);
  STAP_PROBE8(HelperSDT, method__7, arg1.vartype.d, arg2.vartype.d, arg3.vartype.d, arg4.vartype.d, arg5.vartype.d, arg6.vartype.d, arg7.vartype.d, rulename);
}

/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE8
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE8
(JNIEnv *env, jobject obj, jstring _rulename, jobject _arg1, jobject _arg2, jobject _arg3, jobject _arg4, jobject _arg5, jobject _arg6, jobject _arg7, jobject _arg8)
{
  char* rulename = get_java_string(env, _rulename);
  _staparg arg1 = {0}; //initialize to zero so we don't get garbage the first time through
  _staparg arg2 = {0};
  _staparg arg3 = {0};
  _staparg arg4 = {0};
  _staparg arg5 = {0};
  _staparg arg6 = {0};
  _staparg arg7 = {0};
  _staparg arg8 = {0};
  arg1 = determine_java_type(env, _arg1, arg1);
  arg2 = determine_java_type(env, _arg2, arg2);
  arg3 = determine_java_type(env, _arg3, arg3);
  arg4 = determine_java_type(env, _arg4, arg4);
  arg5 = determine_java_type(env, _arg5, arg5);
  arg6 = determine_java_type(env, _arg6, arg6);
  arg7 = determine_java_type(env, _arg7, arg7);
  arg8 = determine_java_type(env, _arg8, arg8);
  if(arg1.type == OTHER || arg1.type == NONE)
    arg1.vartype.c = get_java_string(env, _arg1); // we need to create some type of check for strings
  if(arg2.type == OTHER || arg2.type == NONE)
    arg2.vartype.c = get_java_string(env, _arg2);
  if(arg3.type == OTHER || arg3.type == NONE)
    arg3.vartype.c = get_java_string(env, _arg3);
  if(arg4.type == OTHER || arg4.type == NONE)
    arg4.vartype.c = get_java_string(env, _arg4);
  if(arg5.type == OTHER || arg5.type == NONE)
    arg5.vartype.c = get_java_string(env, _arg5);
  if(arg6.type == OTHER || arg6.type == NONE)
    arg6.vartype.c = get_java_string(env, _arg6);
  if(arg7.type == OTHER || arg7.type == NONE)
    arg7.vartype.c = get_java_string(env, _arg7);
  if(arg8.type == OTHER || arg8.type == NONE)
    arg8.vartype.c = get_java_string(env, _arg8);
  STAP_PROBE9(HelperSDT, method__8, arg1.vartype.d, arg2.vartype.d, arg3.vartype.d, arg4.vartype.d, arg5.vartype.d, arg6.vartype.d, arg7.vartype.d, arg8.vartype.d, rulename);
}
/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE9
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE9
(JNIEnv *env, jobject obj, jstring _rulename, jobject _arg1, jobject _arg2, jobject _arg3, jobject _arg4, jobject _arg5, jobject _arg6, jobject _arg7, jobject _arg8, jobject _arg9)
{
  char* rulename = get_java_string(env, _rulename);
  _staparg arg1 = {0}; //initialize to zero so we don't get garbage the first time through
  _staparg arg2 = {0};
  _staparg arg3 = {0};
  _staparg arg4 = {0};
  _staparg arg5 = {0};
  _staparg arg6 = {0};
  _staparg arg7 = {0};
  _staparg arg8 = {0};
  _staparg arg9 = {0};
  arg1 = determine_java_type(env, _arg1, arg1);
  arg2 = determine_java_type(env, _arg2, arg2);
  arg3 = determine_java_type(env, _arg3, arg3);
  arg4 = determine_java_type(env, _arg4, arg4);
  arg5 = determine_java_type(env, _arg5, arg5);
  arg6 = determine_java_type(env, _arg6, arg6);
  arg7 = determine_java_type(env, _arg7, arg7);
  arg8 = determine_java_type(env, _arg8, arg8);
  arg9 = determine_java_type(env, _arg9, arg9);
  if(arg1.type == OTHER || arg1.type == NONE)
    arg1.vartype.c = get_java_string(env, _arg1); // we need to create some type of check for strings
  if(arg2.type == OTHER || arg2.type == NONE)
    arg2.vartype.c = get_java_string(env, _arg2);
  if(arg3.type == OTHER || arg3.type == NONE)
    arg3.vartype.c = get_java_string(env, _arg3);
  if(arg4.type == OTHER || arg4.type == NONE)
    arg4.vartype.c = get_java_string(env, _arg4);
  if(arg5.type == OTHER || arg5.type == NONE)
    arg5.vartype.c = get_java_string(env, _arg5);
  if(arg6.type == OTHER || arg6.type == NONE)
    arg6.vartype.c = get_java_string(env, _arg6);
  if(arg7.type == OTHER || arg7.type == NONE)
    arg7.vartype.c = get_java_string(env, _arg7);
  if(arg8.type == OTHER || arg8.type == NONE)
    arg8.vartype.c = get_java_string(env, _arg8);
  if(arg9.type == OTHER || arg9.type == NONE)
    arg9.vartype.c = get_java_string(env, _arg9);
  STAP_PROBE10(HelperSDT, method__9, arg1.vartype.d, arg2.vartype.d, arg3.vartype.d, arg4.vartype.d, arg5.vartype.d, arg6.vartype.d, arg7.vartype.d, arg8.vartype.d, arg9.vartype.d, rulename);
}

/*
 * Class:     HelperSDT
 * Method:    METHOD_STAP_PROBE10
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_systemtap_byteman_helper_HelperSDT_METHOD_1STAP_1PROBE10
(JNIEnv *env, jobject obj, jstring _rulename, jobject _arg1, jobject _arg2, jobject _arg3, jobject _arg4, jobject _arg5, jobject _arg6, jobject _arg7, jobject _arg8, jobject _arg9, jobject _arg10)
{
  char* rulename = get_java_string(env, _rulename);
  _staparg arg1 = {0}; //initialize to zero so we don't get garbage the first time through
  _staparg arg2 = {0};
  _staparg arg3 = {0};
  _staparg arg4 = {0};
  _staparg arg5 = {0};
  _staparg arg6 = {0};
  _staparg arg7 = {0};
  _staparg arg8 = {0};
  _staparg arg9 = {0};
  _staparg arg10 = {0};
  arg1 = determine_java_type(env, _arg1, arg1);
  arg2 = determine_java_type(env, _arg2, arg2);
  arg3 = determine_java_type(env, _arg3, arg3);
  arg4 = determine_java_type(env, _arg4, arg4);
  arg5 = determine_java_type(env, _arg5, arg5);
  arg6 = determine_java_type(env, _arg6, arg6);
  arg7 = determine_java_type(env, _arg7, arg7);
  arg8 = determine_java_type(env, _arg8, arg8);
  arg9 = determine_java_type(env, _arg9, arg9);
  arg10 = determine_java_type(env, _arg10, arg10);
  if(arg1.type == OTHER || arg1.type == NONE)
    arg1.vartype.c = get_java_string(env, _arg1); // we need to create some type of check for strings
  if(arg2.type == OTHER || arg2.type == NONE)
    arg2.vartype.c = get_java_string(env, _arg2);
  if(arg3.type == OTHER || arg3.type == NONE)
    arg3.vartype.c = get_java_string(env, _arg3);
  if(arg4.type == OTHER || arg4.type == NONE)
    arg4.vartype.c = get_java_string(env, _arg4);
  if(arg5.type == OTHER || arg5.type == NONE)
    arg5.vartype.c = get_java_string(env, _arg5);
  if(arg6.type == OTHER || arg6.type == NONE)
    arg6.vartype.c = get_java_string(env, _arg6);
  if(arg7.type == OTHER || arg7.type == NONE)
    arg7.vartype.c = get_java_string(env, _arg7);
  if(arg8.type == OTHER || arg8.type == NONE)
    arg8.vartype.c = get_java_string(env, _arg8);
  if(arg9.type == OTHER || arg9.type == NONE)
    arg9.vartype.c = get_java_string(env, _arg9);
  if(arg10.type == OTHER || arg10.type == NONE)
    arg10.vartype.c = get_java_string(env, _arg10);
  STAP_PROBE11(HelperSDT, method__10, arg1.vartype.d, arg2.vartype.d, arg3.vartype.d, arg4.vartype.d, arg5.vartype.d, arg6.vartype.d, arg7.vartype.d, arg8.vartype.d, arg9.vartype.d, arg10.vartype.d, rulename);
}
