import assert from 'node:assert/strict'
import { mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { createHash } from 'node:crypto'
import test from 'node:test'
import { githubRepository, releaseLocal, verifyLinuxX86 } from './release-local.mjs'

const commit = 'a'.repeat(40)
function elf() {
  const binary = Buffer.alloc(52)
  Buffer.from([0x7f, 0x45, 0x4c, 0x46, 1, 1]).copy(binary)
  binary.writeUInt16LE(3, 16)
  binary.writeUInt16LE(3, 18)
  return binary
}

test('origin selection accepts GitHub remotes and rejects foreign hosts or injected arguments', () => {
  for (const remote of ['https://github.com/keenplify/yapb-comms.git', 'git@github.com:keenplify/yapb-comms.git', 'ssh://git@github.com/keenplify/yapb-comms.git']) {
    assert.equal(githubRepository(remote), 'keenplify/yapb-comms')
  }
  for (const remote of ['/tmp/repo', 'https://github.com.evil.test/owner/repo', 'https://github.com/owner/repo;whoami']) {
    assert.throws(() => githubRepository(remote))
  }
})

test('binary gate rejects 64-bit, foreign architecture, executables and truncated files', () => {
  verifyLinuxX86(elf())
  for (const [offset, value] of [[4, 2], [5, 2], [16, 2], [18, 40]]) {
    const binary = elf()
    binary[offset] = value
    assert.throws(() => verifyLinuxX86(binary))
  }
  assert.throws(() => verifyLinuxX86(Buffer.alloc(20)))
})

async function fixture(t, { dirty = false, existing = false, failTests = false, invalidBinary = false, changedCommit = false } = {}) {
  const root = await mkdtemp(join(tmpdir(), 'yapb-release-test-'))
  t.after(() => rm(root, { recursive: true, force: true }))
  await mkdir(join(root, 'build-release-local'))
  const binary = invalidBinary ? Buffer.from('not a module') : elf()
  await writeFile(join(root, 'build-release-local/libyapb.so'), binary)
  const calls = []
  let headReads = 0
  const run = (cmd, args) => {
    calls.push([cmd, ...args])
    if (cmd === 'git' && args[0] === 'status') return dirty ? ' M src/chatlib.cpp' : ''
    if (cmd === 'git' && args[0] === 'rev-parse') return changedCommit && headReads++ > 0 ? 'b'.repeat(40) : commit
    if (cmd === 'git' && args[0] === 'remote') return 'https://github.com/keenplify/yapb-comms.git'
    if (cmd === 'gh' && args[0] === 'api') return args.includes('.sha') ? commit : existing ? `comms-local-${commit}` : ''
    if (cmd === 'ctest' && failTests) throw new Error('behavior regression')
    if (cmd === 'gh' && args[0] === 'release' && args[1] === 'view') {
      return JSON.stringify({ assets: [
        { name: 'yapb-linux-x86.so', size: binary.length },
        { name: 'yapb-linux-x86.so.sha256', size: 84 }
      ] })
    }
    return ''
  }
  return { root, calls, run, binary }
}

test('clean successful release tests before publication and publishes a verified asset pair', async t => {
  const { root, calls, run, binary } = await fixture(t)
  assert.equal(await releaseLocal(root, { run, log() {} }), `comms-local-${commit}`)
  const create = calls.findIndex(call => call[0] === 'gh' && call[1] === 'release' && call[2] === 'create')
  assert.ok(create > calls.findIndex(call => call[0] === 'ctest'))
  assert.ok(create > calls.findIndex(call => call[0] === 'python3'))
  assert.ok(calls[create].includes('--draft'))
  assert.ok(calls.at(-1).includes('--draft=false'))
  assert.ok(calls.every(call => !call.includes('--clobber')))
  const hash = createHash('sha256').update(binary).digest('hex')
  assert.equal(await readFile(join(root, 'dist/local-release', `comms-local-${commit}`, 'yapb-linux-x86.so.sha256'), 'utf8'), `${hash}  yapb-linux-x86.so\n`)
})

for (const [name, options, error] of [
  ['dirty source', { dirty: true }, /Commit or stash/],
  ['existing release', { existing: true }, /already has/],
  ['failed behavior tests', { failTests: true }, /behavior regression/],
  ['wrong binary', { invalidBinary: true }, /ELF 32-bit/],
  ['source changed during build', { changedCommit: true }, /Source commit changed/]
]) {
  test(`${name} cannot create or edit a release`, async t => {
    const { root, calls, run } = await fixture(t, options)
    await assert.rejects(releaseLocal(root, { run, log() {} }), error)
    assert.ok(calls.every(call => !(call[0] === 'gh' && call[1] === 'release')))
  })
}
