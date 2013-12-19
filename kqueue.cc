#include <v8.h>
#include <node.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
//#include "kqueue.h"
#include "nan.h"
using namespace node;
using namespace v8;

class KQueueEmitCB {
public:
    KQueueEmitCB() {
        NanScope();
        v8::Local<v8::Object> obj = v8::Object::New();
        NanAssignPersistent(v8::Object, handle, obj);
    }
    
    KQueueEmitCB(const v8::Handle<v8::Function> &fn) {
        NanScope();
        v8::Local<v8::Object> obj = v8::Object::New();
        NanAssignPersistent(v8::Object, handle, obj);
        SetFunction(fn);
    }
    
    ~KQueueEmitCB() {
        if (handle.IsEmpty()) return;
        handle.Dispose();
        handle.Clear();
    }
    
    NAN_INLINE(void SetFunction(const v8::Handle<v8::Function> &fn)) {
        NanScope();
        NanPersistentToLocal(handle)->Set(NanSymbol("emit"), fn);
    }
    
    NAN_INLINE(v8::Local<v8::Function> GetFunction ()) {
        return NanPersistentToLocal(handle)->Get(NanSymbol("emit"))
        .As<v8::Function>();
    }
    
    void Call(int argc, v8::Handle<v8::Value> argv[]) {
        NanScope();
#if NODE_VERSION_AT_LEAST(0, 8, 0)
        v8::Local<v8::Function> callback = NanPersistentToLocal(handle)->
            Get(NanSymbol("emit")).As<v8::Function>();
        node::MakeCallback(v8::Context::GetCurrent()->Global(), callback, argc, argv);
#else
        node::MakeCallback(handle, "emit", argc, argv);
#endif
        
    }
    
private:
    v8::Persistent<v8::Object> handle;
};

// Taken from https://github.com/joyent/node/blob/master/src/node_file.cc

#define TYPE_ERROR(msg) ThrowException(Exception::TypeError(String::New(msg)));
#define THROW_BAD_ARGS TYPE_ERROR("Invalid arguments");

#define N_STRING(x) String::New(x)
#define N_NUMBER(x) Number::New(x)
#define N_NULL Local<Value>::New(Null())

//static Persistent<String> emit_symbol;
static Persistent<String> errno_symbol;
static Persistent<String> code_symbol;
static Persistent<String> errmsg_symbol;

// Taken from https://github.com/joyent/node/blob/master/src/node.cc
// hack alert! copy of ErrnoException, tuned for kqueue errors

Local<Value> KQueueException(int errorno, const char *code, const char *msg) {
    if (errno_symbol.IsEmpty()) {
        errno_symbol = NODE_PSYMBOL("errno");
        code_symbol = NODE_PSYMBOL("code");
        errmsg_symbol = NODE_PSYMBOL("msg");
    }
    
    if (!msg || !msg[0]) {
        msg = strerror(errorno);
    }
    
    Local<String> estring = String::NewSymbol(strerror(errorno));
    Local<String> message = String::NewSymbol(msg);
    Local<String> cons1 = String::Concat(estring, String::NewSymbol(", "));
    Local<String> cons2 = String::Concat(cons1, message);
    
    Local<Value> e = Exception::Error(cons2);
    
    Local<Object> obj = e->ToObject();
    
    obj->Set(errno_symbol, Integer::New(errorno));
    obj->Set(code_symbol, estring);
    obj->Set(errmsg_symbol, message);
    return obj;
}

class KQueueForkWorker : public NanAsyncWorker {
public:
    KQueueForkWorker(NanCallback *callback, int pid)
    : NanAsyncWorker(callback), pid(pid), forked(0) {}
    ~KQueueForkWorker() {}
    
    void Execute() {
        kq = kqueue();
        if (kq == -1) {
            err = errno;
            printf("Error kqueue: [%d] %s\n", err, strerror(err));
            printf("kq == -1\n");
            errmsg = strerror(err);
            //ThrowException(KQueueException(err, strerror(err), NULL));
            return;
            //HandleErrCallback(err);
        }
        printf("WATCHING PID: [%d]\n", pid);
        EV_SET(&pevent, pid, EVFILT_PROC, EV_ADD, NOTE_FORK, 0, NULL);
        res = kevent(kq, &pevent, 1, NULL, 0, NULL);
        if (res == -1) {
            err = errno;
            printf("Error kevent: [%d] %s\n", err, strerror(err));
            errmsg = strerror(err);
            return;
        }
        while (1) {
            memset(&pevent, 0x00, sizeof(struct kevent));
            kevent(kq, NULL, 0, &pevent, 1, NULL);
//            if (pevent.fflags & NOTE_EXIT) {
//                exited = 1;
//                return;
//            }
            if (pevent.fflags & NOTE_FORK) {
                forked = 1;
                return;
            }
        }
    }
    
    void HandleOKCallback() {
        NanScope();
        printf("ok callback\n");
        callback->Call(0, NULL);
        forked = 0;
        Execute();
    }
    
    void HandleErrorCallback() {
        NanScope();
        printf("error callback\n");
        Local<Value> e = KQueueException(err, strerror(err), NULL);
        Local<Value> argv[] = {
            e
        };
        callback->Call(1, argv);
    }
    
private:
    int pid;
    int forked;
    int kq;
    int err;
    int res;
    struct kevent pevent;
};

class KQueueExitWorker : public NanAsyncWorker {
public:
    KQueueExitWorker(NanCallback *callback, int pid)
    : NanAsyncWorker(callback), pid(pid), exited(0) {}
    ~KQueueExitWorker() {}
    
    void Execute() {
        kq = kqueue();
        if (kq == -1) {
            err = errno;
            printf("Error kqueue: [%d] %s\n", err, strerror(err));
            printf("kq == -1\n");
            errmsg = strerror(err);
            //ThrowException(KQueueException(err, strerror(err), NULL));
            return;
            //HandleErrCallback(err);
        }
        printf("WATCHING PID: [%d]\n", pid);
        EV_SET(&pevent, pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, NULL);
        res = kevent(kq, &pevent, 1, NULL, 0, NULL);
        if (res == -1) {
            err = errno;
            printf("Error kevent: [%d] %s\n", err, strerror(err));
            errmsg = strerror(err);
            return;
        }
        while (1) {
            memset(&pevent, 0x00, sizeof(struct kevent));
            kevent(kq, NULL, 0, &pevent, 1, NULL);
            if (pevent.fflags & NOTE_EXIT) {
                exited = 1;
                return;
            }
//            if (pevent.fflags & NOTE_FORK) {
//                forked = 1;
//                return;
//            }
        }
    }

    void HandleOKCallback() {
        NanScope();
        printf("ok callback\n");
        callback->Call(0, NULL);
    }
    
    void HandleErrorCallback() {
        NanScope();
        printf("error callback\n");
        Local<Value> e = KQueueException(err, strerror(err), NULL);
        Local<Value> argv[] = {
            e
        };
        callback->Call(1, argv);
    }
    
private:
    int pid;
    int exited;
    int kq;
    int err;
    int res;
    struct kevent pevent;
};

NAN_METHOD(WatchExit) {
    NanScope();
    int pid = args[0]->Int32Value();
    NanCallback *callback = new NanCallback(args[1].As<Function>());
    NanAsyncQueueWorker(new KQueueExitWorker(callback, pid));
    NanReturnUndefined();
}

NAN_METHOD(WatchFork) {
    NanScope();
    int pid = args[0]->Int32Value();
    NanCallback *callback = new NanCallback(args[1].As<Function>());
    NanAsyncQueueWorker(new KQueueForkWorker(callback, pid));
    NanReturnUndefined();
}

//struct KQueueBaton {
//  int err;
//  pid_t pid;
//  int kq;
//  int res;
//  uv_work_t req;
//  struct kevent pevent;
//  KQueue* _this;
//};
//


//struct KQueue: ObjectWrap {
//    static Handle<Value> New(const Arguments& args);
//    static Handle<Value> Watch(const Arguments& args);
//};

//struct KQueueBaton {
//  int err;
//  pid_t pid;
//  int kq;
//  int res;
//  uv_work_t req;
//  struct kevent pevent;
//  KQueue* _this;
//};

//  uv_loop_t *loop;

//void KQueue::Init(Handle<Object> target) {
//  HandleScope scope;
//  Local<FunctionTemplate> t = FunctionTemplate::New(KQueue::New);
//  t->InstanceTemplate()->SetInternalFieldCount(1);
//  t->SetClassName(String::NewSymbol("KQueue"));
//  //  t->PrototypeTemplate()->Set(String::NewSymbol("_watch"), FunctionTemplate::New(KQueue::Watch)->GetFunction());
//  emit_symbol = NODE_PSYMBOL("emit");
//  NODE_SET_PROTOTYPE_METHOD(t, "_watch", KQueue::Watch);
//  
//  target->Set(String::NewSymbol("KQueue"), t->GetFunction());
//}
//
//void WatchWork(uv_work_t *req) {
//  KQueueBaton *baton = static_cast<KQueueBaton *>(req->data);
//  printf("listen for events\n");
//  baton->res = kevent(baton->kq, &baton->pevent, 1, NULL, 0, NULL);
//}
//
//void WatchAfterWork(uv_work_t *req) {
//  HandleScope scope;
//  KQueueBaton *baton = static_cast<KQueueBaton *>(req->data);
//  //baton->_this = this;
//  if (baton->res == -1) {
//    baton->err = errno;
//    printf("ERROR:\n");
//    baton->_this->EmitError(baton->err);
//    return;
//  }
//  while (1) {
//    memset(&baton->pevent, 0x00, sizeof(struct kevent));
//    kevent(baton->kq, NULL, 0, &baton->pevent, 1, NULL);
//    if (baton->pevent.fflags & NOTE_EXIT) {
//      printf("Got exit event\n");
//      //Handle<Value> argv[] = { N_STRING("exit"), N_NUMBER(baton->pevent.ident) };
//      baton->_this->HandleExit(baton->pevent.ident);
//      return;
//    }
//  }
//}

//Handle<Value> KQueue::Watch(const Arguments& args) {
//  HandleScope scope;
//  KQueue *self = ObjectWrap::Unwrap<KQueue>(args.This());
//  
//  if (args.Length() != 1) {
//    return THROW_BAD_ARGS;
//  }
//  
//  if (!args[0]->IsInt32()) {
//    return TYPE_ERROR("pid must be a number");
//  }
//  
//  KQueueBaton *baton = new KQueueBaton;
//  baton->pid = args[0]->Int32Value();
//  baton->kq = kqueue();
//  baton->req.data = baton;
//  baton->_this = self;
//  
//  if (baton->kq == -1) {
//    int e = errno;
//    printf("kq == -1\n");
//    self->EmitError(e);
//    return Undefined();
//  }
//  Handle<Value> argv[] = {
//    N_STRING("watching")
//  };
//  MakeCallback(args.This(), "emit", 1, argv);
//  
//  EV_SET(&baton->pevent, baton->pid, EVFILT_PROC, EV_ADD, NOTE_EXIT | NOTE_SIGNAL | NOTE_FORK, 0, NULL);
//  self->loop = uv_default_loop();
//  uv_queue_work(self->loop, &baton->req, WatchWork, (uv_after_work_cb)WatchAfterWork);
//  
//  return scope.Close(Undefined());
//}




//Handle<Value> KQueue::New(const Arguments& args) {
//  HandleScope scope;
//  KQueue *self = new KQueue();
//  self->Wrap(args.This());
//  
//  return scope.Close(args.This());
//}
//
//void KQueue::HandleWatch(int pid) {
//  HandleScope scope;
//  Handle<Value> notice = N_NUMBER(pid);
//  Emit("watch", &notice);
//}
//void KQueue::HandleExit(int pid) {
//  HandleScope scope;
//  printf("HandleExit(%d)\n", pid);
//  Handle<Value> notice = N_NUMBER(pid);
//  Emit("exit", &notice);
//}
//
//void KQueue::HandleSignal(int pid) {
//  HandleScope scope;
//  Handle<Value> notice = N_NUMBER(pid);
//  Emit("signal", &notice);
//}




//void KQueue::Emit(const char *name, Handle<Value>* arg) {
//  HandleScope scope;
//  Handle<Value> args[] = { N_STRING(name), *arg };
//  printf("Emit: %s\n", name);
//  Emit(2, args);
//}
//
//void KQueue::Emit(int length, Handle<Value>* args) {
//  HandleScope scope;
//  TryCatch tc;
//  MakeCallback(this->handle_, "emit", length, args);
//  if (tc.HasCaught()) {
//    FatalException(tc);
//  }
////  Local<Value> emit_v = this->handle_->Get(emit_symbol);
////  assert(emit_v->IsFunction());
////  Local<Function> emit_f = emit_v.As<Function>();
////  printf("Emit: %d\n", length);
////  TryCatch tc;
////  Local<Value> ret = emit_f->Call(this->handle_, length, args);
////  printf("%d", ret->Int32Value());
////  if (tc.HasCaught()) {
////    FatalException(tc);
////  }
//}

//void KQueue::EmitError(int errorno) {
//  HandleScope scope;
//  Local<Value> err = KQueueException(errorno, strerror(errorno), NULL);
//  printf("Emit error: %d - %s\n", errorno, strerror(errorno));
//  KQueue::Emit("error", &err);
//}


//void WatchWork(uv_work_t *req) {
//  KQueueBaton *baton = static_cast<KQueueBaton *>(req->data);
//  printf("listen for events\n");
//  baton->res = kevent(baton->kq, &baton->pevent, 1, NULL, 0, NULL);
//}
//
//void WatchAfterWork(uv_work_t *req) {
//  HandleScope scope;
//  KQueueBaton *baton = static_cast<KQueueBaton *>(req->data);
//  if (baton->res == -1) {
//
//    TryCatch try_catch;
//    Local<Value> err = KQueueException(baton->err, strerror(baton->err), NULL);
//    Handle<Value> argv[] = { N_STRING("error"), err };
//    baton->cb->Call(baton->_this, 2, argv);
//    if (try_catch.HasCaught()) {
//      node::FatalException(try_catch);
//    }
//  }
//  while (1) {
//    memset(&baton->pevent, 0x00, sizeof(struct kevent));
//    kevent(baton->kq, NULL, 0, &baton->pevent, 1, NULL);
//    if (baton->pevent.fflags & NOTE_EXIT) {
//      printf("Got exit event\n");
//      TryCatch try_catch;
//      Handle<Value> argv[] = { N_STRING("exit"), N_NUMBER(baton->pevent.ident) };
//      baton->cb->Call(baton->_this, 2, argv);
//      if (try_catch.HasCaught()) {
//        node::FatalException(try_catch);
//      }
//    }
//  }
//}
//void WatchAfterWork(uv_work_t *req) {
//  KQueueBaton *baton = static_cast<KQueueBaton *>(req->data);
//
//  if (baton->res == -1) {
//    printf("Error occurred: %d %s\n", errno, strerror(errno));
//    ERROR_CB(baton, "Error listening for events");
//
//    return;
//  }
//
//  while (1) {
//    memset(&baton->pevent, 0x00, sizeof(struct kevent));
//
//    kevent(baton->kq, NULL, 0, &baton->pevent, 1, NULL);
//    if (baton->pevent.fflags & NOTE_EXIT) {
//      printf("Got exit event\n");
//      Local<Value> argv[3] = {
//        N_NULL,
//        N_STRING("exit"),
//        N_NUMBER(baton->pevent.ident)
//      };
//      TryCatch try_catch;
//      baton->callback->Call(Context::GetCurrent()->Global(), 3, argv);
//      if (try_catch.HasCaught()) {
//        node::FatalException(try_catch);
//      }
//    }
//
//    if (baton->pevent.fflags & NOTE_SIGNAL) {
//      Local<Value> argv[3] = {
//        N_NULL,
//        N_STRING("signal"),
//        N_NUMBER(baton->pevent.ident)
//      };
//      TryCatch try_catch;
//      baton->callback->Call(Context::GetCurrent()->Global(), 3, argv);
//      if (try_catch.HasCaught()) {
//        node::FatalException(try_catch);
//      }
//    }
//
//    if (baton->pevent.fflags & NOTE_FORK) {
//      Local<Value> argv[3] = {
//        N_NULL,
//        N_STRING("fork"),
//        N_NUMBER(baton->pevent.ident)
//      };
//      TryCatch try_catch;
//      baton->callback->Call(Context::GetCurrent()->Global(), 3, argv);
//      if (try_catch.HasCaught()) {
//        node::FatalException(try_catch);
//      }
//    }
//
//  }
//
//  delete req;
//}

//Handle<Value> KQueue::Watch(const Arguments& args) {
//  HandleScope scope;
//  KQueue *self = ObjectWrap::Unwrap<KQueue>(args.This());
//
//  if (args.Length() != 2) {
//    return THROW_BAD_ARGS
//  }
//
//  if (!args[0]->IsInt32()) {
//    return TYPE_ERROR("pid must be a number");
//  }
//
//  if (!args[1]->IsFunction()) {
//    return TYPE_ERROR("callback must be a function");
//  }
//
//
//
//
//
//  KQueueBaton *baton = new KQueueBaton;
//  baton->_this = args.This();
//  Local<Value> emit_v = Context::GetCurrent()->Global()->Get(emit_symbol);
////  if (!emit_v->IsFunction()) {
////    printf("emit_v is not a function\n");
////    return Undefined();
////  }
//  Local<Function> emit = Local<Function>::Cast(emit_v);
//  baton->req.data = baton;
//  baton->pid = args[0]->Int32Value();
//  baton->kq = kqueue();
//  baton->cb = emit;
//
//  if (baton->kq == -1) {
//    baton->err = errno;
//    TryCatch try_catch;
//    Local<Value> err = KQueueException(baton->err, strerror(baton->err), NULL);
//    Handle<Value> argv[] = { N_STRING("error"), err };
//    emit->Call(args.This(), 2, argv);
//    if (try_catch.HasCaught()) {
//      node::FatalException(try_catch);
//    }
//  }
//
//
//
//  //uv_loop_t *loop = uv_loop_new();
//
//  EV_SET(&baton->pevent, baton->pid, EVFILT_PROC, EV_ADD, NOTE_EXIT | NOTE_SIGNAL | NOTE_FORK, 0, NULL);
//
//
//
//  uv_queue_work(uv_default_loop(), &baton->req, WatchWork, (uv_after_work_cb)WatchAfterWork);
//  //uv_queue_work(loop, &baton->req, WatchWork, (uv_after_work_cb)WatchAfterWork);
//  return Undefined();
//}

//extern "C" void init(Handle<Object> target) {
//  HandleScope scope;
//  KQueue::Init(target);
//}

//NODE_MODULE(kqueue, init);

void InitAll(Handle<Object> exports) {
    exports->Set(NanSymbol("watchExit"), FunctionTemplate::New(WatchExit)->GetFunction());
    exports->Set(NanSymbol("watchFork"), FunctionTemplate::New(WatchFork)->GetFunction());
}

NODE_MODULE(kqueue, InitAll);