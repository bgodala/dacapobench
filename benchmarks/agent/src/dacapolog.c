#include <sys/time.h>

#include <jni.h>

#include "dacapolog.h"
#include "dacapooptions.h"
#include "dacapotag.h"
#include "dacapolock.h"

#include "dacapoallocation.h"
#include "dacapoexception.h"
#include "dacapomethod.h"
#include "dacapomonitor.h"
#include "dacapothread.h"
#include "dacapocallchain.h"

jrawMonitorID       lockLog;
FILE*               logFile = NULL;
jboolean			logState = FALSE;
struct timeval      startTime;
jclass              log_class = NULL;
jmethodID           reportHeapID;
jfieldID            firstReportSinceForceGCID;

jfieldID            callChainCountID;
jfieldID            callChainFrequencyID;
jfieldID            callChainEnableID;

void setLogFileName(const char* log_file) {
	if (logFile!=NULL) {
		fclose(logFile);
		logFile = NULL;
	}
	logFile = fopen(log_file,"w");
}

void callReportHeap(JNIEnv *env) {
	(*env)->CallStaticVoidMethod(env,log_class,reportHeapID);
}

void setReportHeap(JNIEnv *env) {
	(*env)->SetStaticBooleanField(env,log_class,firstReportSinceForceGCID,(jboolean)TRUE);
}

void setReportCallChain(JNIEnv *env, jlong frequency, jboolean enable) {
	if (enable) {
		(*env)->SetStaticLongField(env,log_class,callChainFrequencyID,frequency);
		(*env)->SetStaticLongField(env,log_class,callChainCountID,(jlong)0);
	}
	(*env)->SetStaticBooleanField(env,log_class,callChainEnableID,enable);
}

_Bool dacapo_log_init() {
	if (JVMTI_FUNC_PTR(baseEnv,CreateRawMonitor)(baseEnv, "agent data", &(lockLog)) != JNI_OK)
		return FALSE;

	/* make log file */
	char tmpFile[10240];
	if (isSelected(OPT_LOG_FILE,tmpFile)) {
		setLogFileName(tmpFile);
	}
	
    gettimeofday(&startTime, NULL);
    
	return TRUE;
}


JNIEXPORT void JNICALL Java_org_dacapo_instrument_Agent_localinit
  (JNIEnv *env, jclass klass)
{
	reportHeapID              = (*env)->GetStaticMethodID(env,klass,"reportHeap","()V");
	firstReportSinceForceGCID = (*env)->GetStaticFieldID(env,klass,"firstReportSinceForceGC","Z");
	callChainCountID          = (*env)->GetStaticFieldID(env,klass,"callChainCount","J");
	callChainFrequencyID      = (*env)->GetStaticFieldID(env,klass,"callChainFrequency","J");
	callChainEnableID         = (*env)->GetStaticFieldID(env,klass,"callChainEnable","Z");

	log_class                 = (*env)->NewGlobalRef(env, klass);
}

JNIEXPORT void JNICALL Java_org_dacapo_instrument_Agent_wierd
  (JNIEnv *env, jclass klass)
{
	fprintf(stderr,"Agent_wierd[start]\n");
	callReportHeap(env);
	fprintf(stderr,"Agent_wierd[stop]\n");
}

/*
 * Class:     org_dacapo_instrument_Agent
 * Method:    available
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_org_dacapo_instrument_Agent_available
  (JNIEnv *env, jclass klass)
{
    return !FALSE;
}

/*
 * Class:     org_dacapo_instrument_Agent
 * Method:    log
 * Signature: (Ljava/lang/Thread;Ljava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_dacapo_instrument_Agent_log
  (JNIEnv *env, jclass klass, jobject thread, jstring e, jstring m)
{
    if (logState) {
	    jboolean iscopy_e;
	    jboolean iscopy_m;
	    jlong    thread_tag = 0;
	    jboolean new_thread_tag;

		enterCriticalSection(&lockTag);
		jboolean thread_has_new_tag = getTag(thread, &thread_tag);
		exitCriticalSection(&lockTag);

	    const char *c_e = JVMTI_FUNC_PTR(env,GetStringUTFChars)(env, e, &iscopy_e);
	    const char *c_m = JVMTI_FUNC_PTR(env,GetStringUTFChars)(env, m, &iscopy_m);

	    enterCriticalSection(&lockLog);
	    log_field_string(c_e);
	    
		jniNativeInterface* jni_table;
		if (thread_has_new_tag) {
			jniNativeInterface* jni_table;
			if (JVMTI_FUNC_PTR(baseEnv,GetJNIFunctionTable)(baseEnv,&jni_table) != JNI_OK) {
				fprintf(stderr, "failed to get JNI function table\n");
				exit(1);
			}

			LOG_OBJECT_CLASS(jni_table,env,baseEnv,thread);

			// get class and get thread name.
			jvmtiThreadInfo info;
			JVMTI_FUNC_PTR(baseEnv,GetThreadInfo)(baseEnv, thread, &info);
			log_field_string(info.name);
			if (info.name!=NULL) JVMTI_FUNC_PTR(baseEnv,Deallocate)(baseEnv,(unsigned char*)info.name);
		} else {
			log_field_string(NULL);
			log_field_string(NULL);
		}
		
	    log_field_string(c_m);
	    log_eol();
	    exitCriticalSection(&lockLog);

	    JVMTI_FUNC_PTR(env,ReleaseStringUTFChars)(env, e, c_e);
	    JVMTI_FUNC_PTR(env,ReleaseStringUTFChars)(env, m, c_m);
    }
}

/*
 * Class:     org_dacapo_instrument_Agent
 * Method:    setLogFileName
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_dacapo_instrument_Agent_setLogFileName
  (JNIEnv *env, jclass klass, jstring s)
{
    jboolean iscopy;
    const char *m = JVMTI_FUNC_PTR(env,GetStringUTFChars)(env, s, &iscopy);

    setLogFileName(m);

    JVMTI_FUNC_PTR(env,ReleaseStringUTFChars)(env, s, m);
}

/*
 * Class:     org_dacapo_instrument_Agent
 * Method:    start
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_dacapo_instrument_Agent_start
  (JNIEnv *env, jclass klass)
{
    if (!logState) {
	    logState = logFile != NULL;
	    if (logState) {
		    enterCriticalSection(&lockLog);
	    	log_field_string("START");
	    	log_eol();
		    exitCriticalSection(&lockLog);
	    	
	    	allocation_logon(env);
			exception_logon(env);
			method_logon(env);
			monitor_logon(env);
			thread_logon(env);
			call_chain_logon(env);
	    }
	}
}

/*
 * Class:     org_dacapo_instrument_Agent
 * Method:    stop
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_dacapo_instrument_Agent_stop
  (JNIEnv *env, jclass klass)
{
	jboolean tmp = logState;
    logState = FALSE;
    if (tmp) {
    	log_field_string("STOP");
    	log_eol();
	}
}


/*
 * Class:     org_dacapo_instrument_Agent
 * Method:    reportMonitorEnter
 * Signature: (Ljava/lang/Thread;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_dacapo_instrument_Agent_logMonitorEnter
  (JNIEnv *local_env, jclass klass, jobject thread, jobject object)
{
	// jclass GetObjectClass(JNIEnv *env, jobject obj);
	jlong thread_tag = 0;
	jlong object_tag = 0;

	enterCriticalSection(&lockTag);
	jboolean thread_has_new_tag = getTag(thread, &thread_tag);
	jboolean object_has_new_tag = getTag(object, &object_tag);
	exitCriticalSection(&lockTag);

	enterCriticalSection(&lockLog);
	log_field_string(LOG_PREFIX_MONITOR_AQUIRE);
	log_field_time();
	
	jniNativeInterface* jni_table;
	if (thread_has_new_tag || object_has_new_tag) {
		if (JVMTI_FUNC_PTR(baseEnv,GetJNIFunctionTable)(baseEnv,&jni_table) != JNI_OK) {
			fprintf(stderr, "failed to get JNI function table\n");
			exit(1);
		}
	}

	log_field_jlong(thread_tag);
	if (thread_has_new_tag) {
		LOG_OBJECT_CLASS(jni_table,local_env,baseEnv,thread);
	} else {
		log_field_string(NULL);
	}
	
	log_field_jlong(object_tag);
	if (object_has_new_tag) {
		LOG_OBJECT_CLASS(jni_table,local_env,baseEnv,object);
	} else {
		log_field_string(NULL);
	}

	if (thread_has_new_tag) {
		// get class and get thread name.
		jvmtiThreadInfo info;
		JVMTI_FUNC_PTR(baseEnv,GetThreadInfo)(baseEnv, thread, &info);
		log_field_string(info.name);
		if (info.name!=NULL) JVMTI_FUNC_PTR(baseEnv,Deallocate)(baseEnv,(unsigned char*)info.name);
	} else {
		log_field_string(NULL);
	}
	
	log_eol();
	exitCriticalSection(&lockLog);
}

/*
 * Class:     org_dacapo_instrument_Agent
 * Method:    reportMonitorExit
 * Signature: (Ljava/lang/Thread;Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_org_dacapo_instrument_Agent_logMonitorExit
  (JNIEnv *local_env, jclass klass, jobject thread, jobject object)
{
	// jclass GetObjectClass(JNIEnv *env, jobject obj);
	jlong thread_tag = 0;
	jlong object_tag = 0;

	enterCriticalSection(&lockTag);
	jboolean thread_has_new_tag = getTag(thread, &thread_tag);
	jboolean object_has_new_tag = getTag(object, &object_tag);
	exitCriticalSection(&lockTag);

	enterCriticalSection(&lockLog);
	log_field_string(LOG_PREFIX_MONITOR_RELEASE);
	log_field_time();
	
	jniNativeInterface* jni_table;
	if (thread_has_new_tag || object_has_new_tag) {
		if (JVMTI_FUNC_PTR(baseEnv,GetJNIFunctionTable)(baseEnv,&jni_table) != JNI_OK) {
			fprintf(stderr, "failed to get JNI function table\n");
			exit(1);
		}
	}

	log_field_jlong(thread_tag);
	if (thread_has_new_tag) {
		LOG_OBJECT_CLASS(jni_table,local_env,baseEnv,thread);
	} else {
		log_field_string(NULL);
	}
	
	log_field_jlong(object_tag);
	if (object_has_new_tag) {
		LOG_OBJECT_CLASS(jni_table,local_env,baseEnv,object);
	} else {
		log_field_string(NULL);
	}

	if (thread_has_new_tag) {
		// get class and get thread name.
		jvmtiThreadInfo info;
		JVMTI_FUNC_PTR(baseEnv,GetThreadInfo)(baseEnv, thread, &info);
		log_field_string(info.name);
		if (info.name!=NULL) JVMTI_FUNC_PTR(baseEnv,Deallocate)(baseEnv,(unsigned char*)info.name);
	} else {
		log_field_string(NULL);
	}
	
	log_eol();
	exitCriticalSection(&lockLog);
}

/*
 * Class:     org_dacapo_instrument_Agent
 * Method:    logCallChain
 * Signature: (Ljava/lang/Thread;)V
 */
JNIEXPORT void JNICALL Java_org_dacapo_instrument_Agent_logCallChain
  (JNIEnv *env, jclass klass, jobject thread) {
  	log_call_chain(env, klass, thread);
}

/*
 * Class:     org_dacapo_instrument_Agent
 * Method:    writeHeapReport
 * Signature: (JJJJ)V
 */
JNIEXPORT void JNICALL Java_org_dacapo_instrument_Agent_writeHeapReport(JNIEnv *local_env, jclass klass, jobject thread, jlong used, jlong free, jlong total, jlong max) {
	jlong thread_tag = 0;

	enterCriticalSection(&lockTag);
	jboolean thread_has_new_tag = getTag(thread, &thread_tag);
	exitCriticalSection(&lockTag);

	enterCriticalSection(&lockLog);
	log_field_string(LOG_PREFIX_HEAP_REPORT);

	thread_log(local_env, thread, thread_tag, thread_has_new_tag);
	
	log_field_jlong(used);
	log_field_jlong(free);
	log_field_jlong(total);
	log_field_jlong(max);
	log_eol();    
	exitCriticalSection(&lockLog);
	
    return;
}

/*
 */

static _Bool first_field     = TRUE;
static char  field_separator = ',';
static char  field_delimiter = '\"';
static char  end_of_line     = '\n';

/* */

static void write_field(const char* text, int text_length, _Bool use_delimiter) {
  if (first_field)
    first_field = FALSE;
  else
    fwrite(&field_separator,sizeof(char),1,logFile);
  
  if (use_delimiter) {
    char temp_field[10240];
    int  temp_length = 0;

    temp_field[temp_length++] = field_delimiter;
  
	if (text!=NULL) {
	  int i;
	  for(i=0; i<text_length;++i) {
	    if (text[i]==field_delimiter)
	      temp_field[temp_length++] = field_delimiter;
        temp_field[temp_length++] = field_delimiter;
	  }
	}
    
    temp_field[temp_length++] = field_delimiter;

    fwrite(temp_field,sizeof(char),temp_length,logFile);
  } else {
    if(text!=NULL && 0<text_length)
      fwrite(text,sizeof(char),text_length,logFile);
  }
}

void log_field_string(const char* text) {
  if (text==NULL)
    write_field(NULL,0,TRUE);
  else {
    int text_length = 0;
    _Bool use_delimiter = FALSE;
    
    while(text[text_length]!='\0') {
       use_delimiter = text[text_length]==field_delimiter || text[text_length]==field_separator;
       ++text_length;
    }
    
    write_field(text,text_length,use_delimiter);
  }
}

void log_field_jboolean(jboolean v) {
  log_field_string(v?"true":"false");
}

void log_field_int(int v) {
  char tmp[32];
  sprintf(tmp,"%d",v);
  log_field_string(tmp);
}

void log_field_jlong(jlong v) {
  char tmp[64];
  sprintf(tmp,"%" FORMAT_JLONG,v);
  log_field_string(tmp);
}

void log_field_long(long v) {
  char tmp[64];
  sprintf(tmp,"%ld",v);
  log_field_string(tmp);
}

void log_field_pointer(const void* p) {
  char tmp[64];
  sprintf(tmp,"%" FORMAT_PTR, PTR_CAST(p));
  log_field_string(tmp);
}

void log_field_string_n(const char* text, int field_length) {
  int i;
  _Bool use_delimiter = FALSE;
  
  for(i=0; i<field_length && !use_delimiter; ++i) {
  	use_delimiter = text[i]==field_delimiter || text[i]==field_separator;
  }
  
  write_field(text,field_length,use_delimiter);
}

void log_field_time() {
	struct timeval tv;
    gettimeofday(&tv, NULL);
    long t = (tv.tv_sec - startTime.tv_sec) * 1000000 + (tv.tv_usec - startTime.tv_usec);
    log_field_long(t);
}

void log_eol() {
  fwrite(&end_of_line,sizeof(char),1,logFile);
  first_field = TRUE;
  fflush(logFile);
}


