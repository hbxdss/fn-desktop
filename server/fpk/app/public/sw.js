'use strict';

/**
 * 飞牛终端 · 媒体预览 Service Worker
 *
 * 后台只接受 Authorization: Bearer 令牌鉴权，而 <img>/<video>/<audio>/<iframe>
 * 标签无法携带自定义请求头。这里拦截 /api/files/preview 与 /api/files/download，
 * 为同源 GET 请求补上 Authorization，并原样保留 Range 头以支持视频拖动播放。
 *
 * 令牌保存在 Cache Storage（缓存名 fnos-auth，键 /__fnos_token），
 * 页面通过 postMessage({ type: 'fnos-token', token }) 同步；退出登录时传空字符串清除。
 */

const CACHE_NAME = 'fnos-auth';
const TOKEN_URL = '/__fnos_token';
const PREVIEW_PATHS = ['/api/files/preview', '/api/files/download', '/api/files/download-folder'];

self.addEventListener('install', () => {
  // 立即接管，无需等待旧版本释放
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(self.clients.claim());
});

/** 读取缓存的令牌，失败时返回空字符串（降级为匿名请求） */
async function readToken() {
  try {
    const cache = await caches.open(CACHE_NAME);
    const response = await cache.match(TOKEN_URL);
    if (!response) {
      return '';
    }
    return (await response.text()).trim();
  } catch (err) {
    console.warn('[预览] 读取缓存令牌失败：', err && err.message);
    return '';
  }
}

/** 页面同步令牌：空字符串表示清除 */
async function writeToken(token) {
  try {
    const cache = await caches.open(CACHE_NAME);
    if (token) {
      await cache.put(TOKEN_URL, new Response(token, {
        headers: { 'Content-Type': 'text/plain; charset=utf-8' }
      }));
    } else {
      await cache.delete(TOKEN_URL);
    }
  } catch (err) {
    console.warn('[预览] 写入缓存令牌失败：', err && err.message);
  }
}

self.addEventListener('message', (event) => {
  const data = event.data;
  if (!data || data.type !== 'fnos-token') {
    return;
  }
  const token = typeof data.token === 'string' ? data.token : '';
  event.waitUntil(writeToken(token));
});

self.addEventListener('fetch', (event) => {
  const request = event.request;
  if (request.method !== 'GET') {
    return;
  }

  let url;
  try {
    url = new URL(request.url);
  } catch (err) {
    return;
  }
  if (url.origin !== self.location.origin) {
    return;
  }
  if (PREVIEW_PATHS.indexOf(url.pathname) === -1) {
    return;
  }

  event.respondWith((async () => {
    const token = await readToken();
    const headers = new Headers(request.headers);
    if (token) {
      headers.set('Authorization', `Bearer ${token}`);
    }
    // 视频/音频拖动进度条依赖 Range，这里显式透传，避免个别浏览器丢头
    const range = request.headers.get('Range');
    if (range) {
      headers.set('Range', range);
    }
    try {
      return await fetch(new Request(request.url, {
        method: 'GET',
        headers,
        credentials: 'same-origin',
        redirect: 'follow',
        cache: 'no-store'
      }));
    } catch (err) {
      console.warn('[预览] 代理请求失败：', err && err.message);
      return new Response('媒体预览请求失败，请检查网络连接后重试', {
        status: 502,
        headers: { 'Content-Type': 'text/plain; charset=utf-8' }
      });
    }
  })());
});
