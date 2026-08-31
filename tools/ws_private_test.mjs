import WebSocket from 'ws';

const host = '127.0.0.1';
const port = 9010;   // 注意:需用 -p 9010 启动服务,或改为 9007

// 替换为你自己的登录 token:
//   curl -s -X POST http://127.0.0.1:9007/api/login \
//     -d "username=<user>&password=<pass>" | grep -o '"token":"[^"]*"'
const tokenA = 'REPLACE_WITH_TOKEN_A'; // ta_test
const tokenB = 'REPLACE_WITH_TOKEN_B'; // tb_test
const userA = 'ta_test';
const userB = 'tb_test';

function wsUrl(token) {
  return `ws://${host}:${port}/chat?token=${token}`;
}

function now() { return Math.floor(Date.now()/1000); }

function makeClient(name, token) {
  const url = wsUrl(token);
  const ws = new WebSocket(url);
  ws.on('open', () => console.log(`${name} OPEN`));
  ws.on('error', (e) => console.error(`${name} ERROR`, e.message));
  ws.on('close', (code, reason) => console.log(`${name} CLOSE`, code, reason && reason.toString()));
  ws.on('message', (data) => {
    try {
      const msg = JSON.parse(data.toString());
      console.log(`${name} RECV:`, msg);
    } catch (e) {
      console.log(`${name} RAW:`, data.toString());
    }
  });
  return ws;
}

(async ()=>{
  console.log('Connecting two WS clients...');
  const A = makeClient('A', tokenA);
  const B = makeClient('B', tokenB);

  // wait for both to open
  await new Promise((res) => setTimeout(res, 800));

  const privateMsgFromA = {
    type: 'private',
    from: userA,
    to: userB,
    content: 'Hello tb_test — this is ta_test (private)',
    timestamp: now(),
    seq: 1
  };

  const privateMsgFromB = {
    type: 'private',
    from: userB,
    to: userA,
    content: 'Reply from tb_test to ta_test',
    timestamp: now(),
    seq: 2
  };

  console.log('A -> B (private)');
  A.send(JSON.stringify(privateMsgFromA));

  // send B->A after short delay
  await new Promise((res) => setTimeout(res, 500));
  console.log('B -> A (private)');
  B.send(JSON.stringify(privateMsgFromB));

  // wait for messages to be exchanged
  await new Promise((res) => setTimeout(res, 2000));

  console.log('Closing clients');
  A.close();
  B.close();
})();
