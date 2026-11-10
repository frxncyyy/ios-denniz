# Package ping: connectivity test

Owned files (only you may edit these): ['gpu/videotest/ping.txt']
Depends on: []

## Deliverable
Write the single line 'pong' into $NORD2_WORK/gpu/videotest/ping.txt, then post one board message to 'root' with subject 'PING OK'. Nothing else.

## Host test oracle
cat the file

## Acceptance
file exists

## Details
{
 "id": "ping",
 "title": "connectivity test",
 "owned_files": [
  "gpu/videotest/ping.txt"
 ],
 "depends_on": [],
 "deliverable": "Write the single line 'pong' into $NORD2_WORK/gpu/videotest/ping.txt, then post one board message to 'root' with subject 'PING OK'. Nothing else.",
 "host_test_oracle": "cat the file",
 "acceptance_check": "file exists"
}
