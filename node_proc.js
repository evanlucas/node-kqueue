console.log(process.pid)

var http = require('http')

var server = http.createServer(function(req, res) {
  var child = require('child_process').fork('./child')
  child.on('exit', function(c) {
    return res.end()
  })
})

server.listen(4040)