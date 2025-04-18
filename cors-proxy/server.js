// server.js
const corsAnywhere = require('cors-anywhere');

const host = 'localhost';
const port = 8080;

corsAnywhere.createServer({
    originWhitelist: [], // Allow all origins
}).listen(port, host, () => {
    console.log(`Running CORS Anywhere on ${host}:${port}`);
});
