# Cloudflare Pages 自動部署設置指南

## 概述

本專案已配置 GitHub Actions，當推送到 `main` 或 `master` 分支時，會自動構建並部署到 Cloudflare Pages。

## 設置步驟

### 1. 獲取 Cloudflare API Token

1. 登入 [Cloudflare Dashboard](https://dash.cloudflare.com/)
2. 點擊右上角的用戶頭像 → **My Profile**
3. 點擊 **API Tokens** 標籤
4. 點擊 **Create Token**
5. 使用 **Custom token** 模板
6. 設置權限：
   - **Account** - `Cloudflare Pages:Edit`
   - **Zone** - `Zone:Read`（如果需要自定義域名）
7. 點擊 **Continue to summary** → **Create Token**
8. **複製並保存此 token**（只會顯示一次）

### 2. 獲取 Cloudflare Account ID

1. 在 [Cloudflare Dashboard](https://dash.cloudflare.com/) 首頁
2. 右側欄位中找到 **Account ID**
3. 點擊 **Click to copy** 複製

### 3. 在 GitHub 設置 Secrets

1. 前往你的 GitHub repo
2. 點擊 **Settings** → **Secrets and variables** → **Actions**
3. 點擊 **New repository secret**
4. 添加以下兩個 secrets：

   - **Name**: `CLOUDFLARE_API_TOKEN`
     **Value**: 步驟 1 獲取的 API Token

   - **Name**: `CLOUDFLARE_ACCOUNT_ID`
     **Value**: 步驟 2 獲取的 Account ID

### 4. 推送代碼觸發部署

完成上述設置後，每次推送到 `main` 或 `master` 分支時，GitHub Actions 會自動：

1. 安裝 Node.js 依賴
2. 安裝 Emscripten
3. 編譯 WASM
4. 構建 Vite 專案
5. 部署到 Cloudflare Pages

## 手動觸發部署

你也可以在 GitHub Actions 頁面手動觸發部署：

1. 前往 **Actions** 標籤
2. 選擇 **Deploy to Cloudflare Pages** workflow
3. 點擊 **Run workflow**

## 檢查部署狀態

- **GitHub**: 前往 **Actions** 標籤查看 workflow 運行狀態
- **Cloudflare**: 前往 [Cloudflare Dashboard](https://dash.cloudflare.com/) → **Pages** → **web-a2e** 查看部署歷史

## 故障排除

### WASM 編譯失敗
- 確認 Emscripten 版本兼容性
- 檢查 `CMakeLists.txt` 配置

### 部署失敗
- 確認 API Token 權限正確
- 確認 Account ID 正確
- 檢查 GitHub Actions 日誌獲取詳細錯誤信息

### 構建時間過長
- 考慮使用 GitHub Actions cache 來緩存 Emscripten 和 node_modules
- 可以在 workflow 中添加 cache 步驟

## 相關連結

- [Cloudflare Pages 文檔](https://developers.cloudflare.com/pages/)
- [Wrangler CLI 文檔](https://developers.cloudflare.com/workers/wrangler/)
- [GitHub Actions 文檔](https://docs.github.com/en/actions)
