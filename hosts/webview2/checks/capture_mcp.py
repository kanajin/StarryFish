"""Exercise stdio mode without starting a browser or accessing a profile."""
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    executable = Path(sys.argv[1])
    requests = [
        {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize',
         'params': {'protocolVersion': '2025-06-18', 'capabilities': {},
                    'clientInfo': {'name': 'isolated-check', 'version': '1'}}},
        {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
        {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
        {'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call',
         'params': {'name': 'gbf_capture_start', 'arguments': {}}},
    ]
    result = subprocess.run([str(executable), '--capture-mcp', '--browser-pid', str(os.getpid())],
                            input=''.join(json.dumps(item) + '\n' for item in requests),
                            capture_output=True, text=True, timeout=15, check=True,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    replies = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(replies) == 3 and not result.stderr
    assert replies[0]['result']['protocolVersion'] == '2025-06-18'
    tools = replies[1]['result']['tools']
    assert {tool['name'] for tool in tools} == {
        'gbf_capture_start', 'gbf_capture_status', 'gbf_capture_read',
        'gbf_capture_stop', 'gbf_capture_clear'}
    assert replies[2]['result']['isError']
    error = json.loads(replies[2]['result']['content'][0]['text'])['error']
    assert error == 'browser_capture_endpoint_unavailable'
    print('PASS MCP stdio initialize/discovery/notification/unavailable-instance; browser NOT_STARTED')


if __name__ == '__main__':
    main()
