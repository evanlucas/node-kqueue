var util = require('util')
  , events = require('events')
  , _KQueue = require('bindings')('kqueue')

module.exports = KQueue


//KQueue.prototype.__proto__ = events.EventEmitter.prototype

//function KQueue() {}
function KQueue(pid) {
  if (!(this instanceof KQueue)) return new KQueue()
  events.EventEmitter.call(this)
  console.log(_KQueue)
  var self = this
  this.pid = pid
  this.error = false
  this.on('fork', function() {
    self._watchFork()
  })
}

util.inherits(KQueue, events.EventEmitter)

KQueue.prototype._watchFork = function() {
  var self = this
  _KQueue.watchFork(self.pid, function(err) {
    if (err) {
      if (!self.error) {
        self.error = true
        self.emit('error', err)
      }
    } else {
      console.log('emit: fork')
      self.emit('fork')
    }
  })
  return this
}

KQueue.prototype._watchExit = function() {
  var self = this
  _KQueue.watchExit(self.pid, function(err) {
    if (err) {
      if (!self.error) {
        self.error = true
        self.emit('error', err)
      }
    } else {
      console.log('emit: exit')
      self.emit('exit')
    }
  })
  return this
}

KQueue.prototype.watch = function(pid) {
  var self = this
  self.emit('watching')
  self._watchFork()
  self._watchExit()

  return this
}

