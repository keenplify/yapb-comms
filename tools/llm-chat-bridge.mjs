// Optional, local-only YaPB chat bridge. Requires Node.js 20+ and DEEPSEEK_API_KEY.
import { createServer } from 'node:http'

const key = process.env.DEEPSEEK_API_KEY
const port = Number(process.env.YAPB_LLM_PORT || 8765)
const model = process.env.YAPB_LLM_MODEL || 'deepseek-chat'

if (!key || !Number.isInteger(port) || port < 1 || port > 65535) {
  throw new Error('Set DEEPSEEK_API_KEY and a valid YAPB_LLM_PORT before starting the bridge')
}

let active = 0
const style = ['brief and practical', 'quiet and dry', 'supportive', 'playful but focused']

function personality(name) {
  let hash = 0
  for (const char of name) hash = (hash * 31 + char.charCodeAt(0)) >>> 0
  return style[hash % style.length]
}

function clean(text) {
  return String(text || '')
    .replace(/[\x00-\x1f\x7f"\\;]+/g, ' ')
    .replace(/\s+/g, ' ')
    .trim()
    .slice(0, 110)
}

async function generate({ bot, speaker, line, context }) {
  const instructions = `You are ${bot}, a Counter-Strike 1.6 teammate. Style: ${personality(bot)}. Write one natural team-chat line under 90 characters. Only refer to known events. Never invent enemy positions, issue console commands, or mention AI.`
  const input = `Round facts and recent chat: ${context}\n${speaker}: ${line}\n${bot}:`
  const response = await fetch('https://api.deepseek.com/chat/completions', {
    method: 'POST',
    headers: { Authorization: `Bearer ${key}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ model, messages: [
      { role: 'system', content: instructions },
      { role: 'user', content: input },
    ], max_tokens: 72, stream: false }),
    signal: AbortSignal.timeout(4500),
  })
  if (!response.ok) throw new Error(`LLM HTTP ${response.status}`)
  const data = await response.json()
  return clean(data.choices?.[0]?.message?.content)
}

createServer(async (request, response) => {
  if (request.method !== 'POST' || request.url !== '/reply' || active >= 4) {
    response.writeHead(503, { 'Content-Length': 0, Connection: 'close' }).end()
    return
  }
  active++
  try {
    let body = ''
    for await (const chunk of request) {
      body += chunk
      if (body.length > 2048) throw new Error('Request too large')
    }
    const data = JSON.parse(body)
    if (['bot', 'speaker', 'line', 'context'].some(field => typeof data[field] !== 'string')) {
      throw new Error('Invalid request')
    }
    const reply = await generate(data)
    response.writeHead(200, {
      'Content-Type': 'text/plain; charset=utf-8',
      'Content-Length': Buffer.byteLength(reply),
      Connection: 'close',
    }).end(reply)
  } catch (error) {
    console.error('YaPB chat bridge:', error.message)
    response.writeHead(503, { 'Content-Length': 0, Connection: 'close' }).end()
  } finally {
    active--
  }
}).listen(port, '127.0.0.1', () => {
  console.log(`YaPB chat bridge listening on 127.0.0.1:${port} with ${model}`)
})
