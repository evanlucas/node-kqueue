{
  "targets": [
    {
      "target_name": "kqueue",
      "sources": ['kqueue.cc'],
      "conditions": [
        ['OS=="mac"', {
          "defines" : [ '__MACOSX_CORE' ],
          "cflags": [
            '-std=c++0x'
          ],
          "include_dirs": [
            "<!(node -e \"require('nan')\")"
          ]
        }]
      ]
    }
  ]
}
