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

async function fixture(t, { dirty = false, existing = false, failTests = false, invalidBinary = false, changedCommit = false, detached = false, conflicts = false, failPush = false, failInit = false, dirtyDuringBuild = false, submodules = '7efccddc085dbaef20e1310f3e270f07cb2b0eff ext/crlib (heads/master-13-g7efccdd)\n a21540a52d7559333ad9d8326411daf78af62891 ext/linkage (a21540a)' } = {}) {
  const root = await mkdtemp(join(tmpdir(), 'yapb-release-test-'))
  t.after(() => rm(root, { recursive: true, force: true }))
  await mkdir(join(root, 'build-release-local'))
  const binary = invalidBinary ? Buffer.from('not a module') : elf()
  await writeFile(join(root, 'build-release-local/libyapb.so'), binary)
  const calls = []
  let headReads = 0
  let built = false
  const run = (cmd, args) => {
    calls.push([cmd, ...args])
    if (cmd === 'git' && args[0] === 'status') return dirty || (built && dirtyDuringBuild) ? ' M src/chatlib.cpp' : ''
    if (cmd === 'git' && args[0] === 'branch') return detached ? '' : 'main'
    if (cmd === 'git' && args[0] === 'diff') return conflicts ? 'src/chatlib.cpp' : ''
    if (cmd === 'git' && args[0] === 'commit') dirty = false
    if (cmd === 'git' && args[0] === 'push' && failPush) throw new Error('push rejected')
    if (cmd === 'cmake' && args[0] === '--build') built = true
    if (cmd === 'git' && args[0] === 'rev-parse') return changedCommit && headReads++ > 0 ? 'b'.repeat(40) : commit
    if (cmd === 'git' && args[0] === 'remote') return 'https://github.com/keenplify/yapb-comms.git'
    if (cmd === 'git' && args[0] === 'submodule') {
      if (args[1] === 'update' && !failInit) submodules = ' ' + commit + ' ext/crlib'
      return args[1] === 'status' ? submodules : ''
    }
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
  ['detached HEAD', { detached: true }, /detached HEAD/],
  ['merge conflicts', { conflicts: true }, /merge conflicts/],
  ['push failure', { failPush: true }, /push rejected/],
  ['source dirtied during build', { dirtyDuringBuild: true }, /appeared during the build/],
  ['existing release', { existing: true }, /already has/],
  ['failed behavior tests', { failTests: true }, /behavior regression/],
  ['wrong binary', { invalidBinary: true }, /ELF 32-bit/],
  ['source changed during build', { changedCommit: true }, /Source commit changed/],
  ['uninitialized submodule', { submodules: `-${commit} ext/crlib`, failInit: true }, /pinned commits/],
  ['unpinned submodule', { submodules: `+${commit} ext/crlib` }, /pinned commits/],
  ['conflicted submodule', { submodules: `U${commit} ext/crlib` }, /pinned commits/]
]) {
  test(`${name} cannot create or edit a release`, async t => {
    const { root, calls, run } = await fixture(t, options)
    await assert.rejects(releaseLocal(root, { run, log() {} }), error)
    assert.ok(calls.every(call => !(call[0] === 'gh' && call[1] === 'release')))
  })
}


test('one command commits pending changes and pushes the exact branch before building', async t => {
  const { root, calls, run } = await fixture(t, { dirty: true })
  await releaseLocal(root, { run, log() {} })
  const stage = calls.findIndex(call => call[0] === 'git' && call[1] === 'add')
  const commitCall = calls.findIndex(call => call[0] === 'git' && call[1] === 'commit')
  const push = calls.findIndex(call => call[0] === 'git' && call[1] === 'push')
  const build = calls.findIndex(call => call[0] === 'cmake')
  assert.ok(stage >= 0 && commitCall > stage && push > commitCall && build > push)
  assert.deepEqual(calls[stage], ['git', 'add', '--all'])
  assert.ok(calls[commitCall].at(-1).includes('[skip ci]'))
  assert.deepEqual(calls[push], ['git', 'push', 'origin', 'HEAD:refs/heads/main'])
})

test('a clean checkout is pushed without creating an empty preparation commit', async t => {
  const { root, calls, run } = await fixture(t)
  await releaseLocal(root, { run, log() {} })
  assert.ok(calls.some(call => call[0] === 'git' && call[1] === 'push'))
  assert.ok(calls.every(call => !(call[0] === 'git' && ['add', 'commit'].includes(call[1]))))
})

test('missing submodules are automatically initialized before building', async t => {
  const { root, calls, run } = await fixture(t, { submodules: `-${commit} ext/crlib` })
  await releaseLocal(root, { run, log() {} })
  assert.ok(calls.some(call => call[0] === 'git' && call[1] === 'submodule' && call[2] === 'update'))
})
