#include <v8.h>
#include <node.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include "nan.h"


using namespace node;
using namespace v8;


class KQueue: public ObjectWrap {
public:
    uv_loop_t *loop;
    static void Init(Handle<Object> target);
    
    static Handle<Value> Watch(const Arguments& args);
    void HandleExit(int pid);
    
    void HandleSignal(int pid);
    
    void Emit(const char* name, Handle<Value>* arg);
    
    void Emit(int length, Handle<Value>* args);
    
    void EmitError(int errorno);
    
protected:
    static Handle<Value> New(const Arguments& args);
    
private:
    void HandleWatch(int pid);
    
    
    
//    void WatchWork(uv_work_t *req);
//    
//    void WatchAfterWork(uv_work_t *req);
    
};

