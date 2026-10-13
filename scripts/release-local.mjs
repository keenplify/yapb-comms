import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { copyFile, mkdir, readFile, writeFile } from 'node:fs/promises'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

export function githubRepository(remote) {
  const match = /^(?:https:\/\/github\.com\/|git@github\.com:|ssh:\/\/git@github\.com\/)([\w.-]+\/[\w.-]+?)(?:\.git)?\/?$/.exec(remote.trim())
  if (!match) throw new Error('Origin must be a GitHub HTTPS or SSH repository URL')
  return match[1]
}

export function verifyLinuxX86(binary) {
  if (binary.length < 52 || !binary.subarray(0, 6).equals(Buffer.from([0x7f, 0x45, 0x4c, 0x46, 1, 1]))
      || binary.readUInt16LE(16) !== 3 || binary.readUInt16LE(18) !== 3) {
    throw new Error('Release binary must be an ELF 32-bit Linux x86 shared library')
  }
}

function execute(command, args, options) {
  return execFileSync(command, args, {
    ...options,
    encoding: 'utf8',
    stdio: options.capture ? ['ignore', 'pipe', 'inherit'] : 'inherit'
  })?.trim() ?? ''
}

export async function releaseLocal(root, { run = execute, log = console.log } = {}) {
  if (process.platform !== 'linux' || !['x64', 'ia32'].includes(process.arch)) {
    throw new Error('Local YaPB releases require a Linux x86 host with a 32-bit C++ toolchain')
  }
  const capture = (cmd, args) => run(cmd, args, { cwd: root, capture: true })
  const invoke = (cmd, args) => run(cmd, args, { cwd: root })
  const assertClean = () => {
    if (capture('git', ['status', '--porcelain', '--untracked-files=all', '--ignore-submodules=none'])) {
      throw new Error('Commit or stash YaPB changes before releasing; uncommitted binaries cannot be pinned to a commit')
    }
    const submodules = capture('git', ['submodule', 'status', '--recursive'])
    if (submodules.split('\n').some(line => /^[+-U]/.test(line))) {
      throw new Error('Initialize submodules at their pinned commits before releasing')
    }
  }
  assertClean()
  const commit = capture('git', ['rev-parse', 'HEAD'])
  if (!/^[a-f0-9]{40}$/.test(commit)) throw new Error('Invalid source commit')
  const repo = githubRepository(capture('git', ['remote', 'get-url', 'origin']))
  // CI publishes comms-<sha> on every main push; use an independent tag so its
  // upload step cannot overwrite a locally built release for the same source.
  const tag = `comms-local-${commit}`
  invoke('gh', ['auth', 'status', '--hostname', 'github.com'])
  // Read-only preflight: publishing must refer to source already on GitHub.
  const remoteCommit = capture('gh', ['api', `repos/${repo}/commits/${commit}`, '--jq', '.sha'])
  if (remoteCommit !== commit) throw new Error('Push this source commit to GitHub before releasing')
  const assertNewRelease = () => {
    const tags = capture('gh', ['api', '--paginate', `repos/${repo}/releases?per_page=100`, '--jq', '.[].tag_name'])
    if (tags.split('\n').includes(tag)) throw new Error(`${tag} already has a GitHub release; refusing to overwrite its assets`)
  }
  assertNewRelease()

  log(`Building ${tag} locally for Linux x86...`)
  const build = join(root, 'build-release-local')
  invoke('cmake', ['-S', root, '-B', build, '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON',
    '-DCMAKE_CXX_FLAGS=-m32', '-DCMAKE_SHARED_LINKER_FLAGS=-m32'])
  invoke('cmake', ['--build', build, '--parallel', '4'])
  invoke('ctest', ['--test-dir', build, '--output-on-failure'])
  invoke('python3', ['-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_*.py'])
  assertClean()
  if (capture('git', ['rev-parse', 'HEAD']) !== commit) throw new Error('Source commit changed during the build; rerun the release')

  const binary = await readFile(join(build, 'libyapb.so'))
  verifyLinuxX86(binary)
  const assets = join(root, 'dist', 'local-release', tag)
  await mkdir(assets, { recursive: true })
  const binaryPath = join(assets, 'yapb-linux-x86.so')
  const checksumPath = `${binaryPath}.sha256`
  await copyFile(join(build, 'libyapb.so'), binaryPath)
  const hash = createHash('sha256').update(binary).digest('hex')
  if (createHash('sha256').update(await readFile(binaryPath)).digest('hex') !== hash) {
    throw new Error('Release binary changed while staging')
  }
  await writeFile(checksumPath, `${hash}  yapb-linux-x86.so\n`)
  const notes = join(assets, 'release-notes.md')
  await writeFile(notes, `Linux x86 Counter-Strike 1.6 YaPB comms module built locally from commit ${commit}.\n\nBot behavior and Python tests passed.\n\nSHA-256: \`${hash}\`\n`)
  assertNewRelease()
  // Stage the complete asset pair before making the release public. If an
  // upload fails, leave the draft intact for inspection, never delete a release.
  invoke('gh', ['release', 'create', tag, '--repo', repo, '--target', commit, '--draft',
    '--title', `YaPB comms ${commit.slice(0, 12)}`, '--notes-file', notes, '--generate-notes'])
  invoke('gh', ['release', 'upload', tag, '--repo', repo, binaryPath, checksumPath])
  const uploaded = JSON.parse(capture('gh', ['release', 'view', tag, '--repo', repo, '--json', 'assets']))
  const actual = new Map(uploaded.assets.map(asset => [asset.name, asset.size]))
  if (actual.size !== 2 || actual.get('yapb-linux-x86.so') !== binary.length
      || actual.get('yapb-linux-x86.so.sha256') !== Buffer.byteLength(`${hash}  yapb-linux-x86.so\n`)) {
    throw new Error('Uploaded release assets are incomplete; the release remains a draft')
  }
  invoke('gh', ['release', 'edit', tag, '--repo', repo, '--draft=false'])
  log(`Published https://github.com/${repo}/releases/tag/${tag}`)
  return tag
}

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  releaseLocal(root).catch(error => {
    console.error(error.message)
    process.exitCode = 1
  })
}
