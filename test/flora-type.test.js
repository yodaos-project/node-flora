'use strict'

var test = require('tape')
var flora = require('..')
var Agent = flora.Agent
var agentOptions = { reconnInterval: 10000, bufsize: 0 }
var okUri = 'unix:/var/run/flora.sock'
var crypto = require('crypto')

test('flora write message types', t => {
  var recvClient = new Agent(okUri, agentOptions)

  var msgId = crypto.randomBytes(5).toString('hex')
  var msgName = `instant msg test one[${msgId}]`

  var writeMsg = [32, 64.0, 'hello flora', null, undefined, ['123']]
  var expectedMsg = [32, 64.0, 'hello flora', undefined, undefined, ['123']]
  recvClient.subscribe(msgName, (msg, type) => {
    t.deepEqual(msg, expectedMsg)
    t.end()
    recvClient.close()
  })
  recvClient.start()
  var postClient = new Agent(okUri, agentOptions)
  postClient.start()
  postClient.post(msgName, writeMsg)
  postClient.close()
})
