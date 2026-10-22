#!/usr/bin/env node
// Optional fixture maintenance only. Qt tests consume the committed corpus.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {createVariedFixture, sustainedPlans} = require('./varied-fixture.cjs');
for (const targetPages of [30, 100]) {
  const fixture = createVariedFixture(__dirname, {targetPages});
  // Keep generated metadata relocatable and independent of the checkout path.
  const {id, units, anchors} = fixture;
  fs.writeFileSync(path.join(fixture.root, 'fixture.json'),
    JSON.stringify({id, targetPages, units, anchors}, null, 2) + '\n');
}
fs.writeFileSync(path.join(__dirname, 'plans.json'), JSON.stringify(sustainedPlans(), null, 2) + '\n');
