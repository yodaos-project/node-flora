'use strict'

var test = require('tape')
var flora = require('..')
var Agent = flora.Agent
var agentOptions = { reconnInterval: 10000, bufsize: 0 }
var okUri = 'unix:/var/run/flora.sock'

test('agent.unref', t => {
  var recvClient = new Agent(okUri, agentOptions)
  recvClient.start({ refLoop: false })
  t.end()
})
