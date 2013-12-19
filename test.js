var pid = process.argv[2]

console.log(process.pid)
var KQueue = require('./kqueue')



var cp = require('child_process')
  , fork = cp.fork

function spawnMe(cb) {
  console.log('spawning child')
  var child = cp.fork('./child')

  child.on('exit', function(c) {
    return cb && cb(c)
  })
}

/*
kqueue.once('exit', function(pid) {
  console.log('test', 'exit:', pid)
})

kqueue.on('signal', function(signal) {
  console.log('signal:', signal)
})

kqueue.on('fork', function(fork) {
  console.log('fork', fork)
})

kqueue.once('error', function(err) {
  console.log('error', err)
})
*/
//console.log(kqueue)

/*
kqueue.watch(+pid, function() {
  console.log('callback called')
})
*/

var child = new KQueue(pid).watch()

child.on('error', function(err) {
  console.log('test: error', err)
})

child.on('fork', function() {
  console.log('test: fork')
})

child.on('exit', function() {
  console.log('test: exit')
})

//console.log(child)

/*
setTimeout(function() {
  spawnMe()
}, 3000)
*/