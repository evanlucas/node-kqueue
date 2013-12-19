console.log('child:', 'spawn')
var t = 0
for (var i=0; i<200000; i++) {
  //console.log(i)
  if (i % 1000 === 0) {
    //console.log(i)
  }
}

console.log('child:', 'pre-exit')
process.exit()