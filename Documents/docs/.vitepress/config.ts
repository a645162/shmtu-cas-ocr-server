import { defineConfig } from 'vitepress'

function resolveBase() {
  const repo = process.env.GITHUB_REPOSITORY?.split('/')[1]
  if (!process.env.GITHUB_ACTIONS || !repo) {
    return '/'
  }
  return repo.endsWith('.github.io') ? '/' : `/${repo}/`
}

export default defineConfig({
  base: resolveBase(),
  lang: 'zh-CN',
  title: 'SHMTU CAS OCR Server 文档',
  description: '上海海事大学统一认证平台验证码识别服务器文档',
  cleanUrls: true,
  lastUpdated: true,
  themeConfig: {
    nav: [
      { text: '快速开始', link: '/guide/get-started' },
      { text: 'Docker部署', link: '/guide/docker-deploy' },
      { text: 'API接口', link: '/guide/api' },
    ],
    sidebar: [
      {
        text: '使用指南',
        items: [
          { text: '快速开始', link: '/guide/get-started' },
          { text: 'Docker部署', link: '/guide/docker-deploy' },
          { text: 'API接口', link: '/guide/api' },
          { text: '配置参数', link: '/guide/config' },
          { text: '模型管理', link: '/guide/model-management' },
          { text: 'FAQ', link: '/guide/faq' },
        ],
      },
    ],
    outline: [2, 3],
    search: {
      provider: 'local',
    },
    footer: {
      message: 'SHMTU CAS OCR Server Docs',
      copyright: 'Copyright © SHMTU CAS OCR',
    },
  },
})
