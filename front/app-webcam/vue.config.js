module.exports = {
  productionSourceMap: false,
  devServer: {
    proxy: {
      '/api': {
        target: 'http://esp32-cam2:80',
        changeOrigin: true,
        ws: true
      }
    }
  }
}