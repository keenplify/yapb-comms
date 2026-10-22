import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { mkdtemp, writeFile, rm } from 'node:fs/promises'
import { createServer } from 'node:net'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import test from 'node:test'

test('bridge handles four thoughts at once and sanitizes replies', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'yapb-bridge-'))
  const mock = join(dir, 'mock.mjs')
  await writeFile(mock, `globalThis.fetch = async (url, options) => {
    const body = JSON.parse(options.body)
    if (url !== 'https://api.deepseek.com/chat/completions'
      || options.headers.Authorization !== 'Bearer test-key'
      || body.model !== 'deepseek-chat' || body.max_tokens !== 72
      || body.messages?.[0]?.role !== 'system' || body.messages?.[1]?.role !== 'user') {
      throw new Error('Invalid DeepSeek request')
    }
    await new Promise(resolve => setTimeout(resolve, 180))
    return new Response(JSON.stringify({ choices: [{ message: { content: 'Nice shot!";quit' } }] }), { status: 200 })
  }`)

  const listener = createServer()
  await new Promise(resolve => listener.listen(0, '127.0.0.1', resolve))
  const port = listener.address().port
  await new Promise(resolve => listener.close(resolve))

  const bridge = fileURLToPath(new URL('./llm-chat-bridge.mjs', import.meta.url))
  const child = spawn(process.execPath, ['--import', mock, bridge], {
    env: { ...process.env, DEEPSEEK_API_KEY: 'test-key', YAPB_LLM_PORT: String(port) },
    stdio: ['ignore', 'pipe', 'pipe'],
  })

  try {
    await new Promise((resolve, reject) => {
      let output = ''
      child.stdout.on('data', chunk => {
        output += chunk
        if (output.includes('listening')) resolve()
      })
      child.on('exit', code => reject(new Error(`Bridge exited: ${code}`)))
      setTimeout(() => reject(new Error('Bridge did not start')), 3000)
    })

    const input = JSON.stringify({ bot: 'Bot', speaker: 'Player', line: 'nice?', context: 'round:1' })
    const replies = await Promise.all(Array.from({ length: 5 }, () => fetch(`http://127.0.0.1:${port}/reply`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: input,
    })))
    assert.equal(replies.filter(reply => reply.status === 200).length, 4)
    assert.equal(replies.filter(reply => reply.status === 503).length, 1)
    assert.equal(await replies[0].text(), 'Nice shot! quit')
  } finally {
    child.kill()
    await rm(dir, { recursive: true, force: true })
  }
})
