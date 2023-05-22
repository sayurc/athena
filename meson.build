project('Athena', 'c', license : 'GPLv3', version : '1.0.0',
        default_options : ['c_std=c17'])

cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required : false)
thread_dep = dependency('threads')
src = ['src/bit.c',
       'src/bit.h',
       'src/eval.c',
       'src/eval.h',
       'src/main.c',
       'src/move.c',
       'src/move.h',
       'src/movegen.c',
       'src/movegen.h',
       'src/pos.c',
       'src/pos.h',
       'src/rng.c',
       'src/rng.h',
       'src/search.c',
       'src/search.h',
       'src/str.c',
       'src/str.h',
       'src/threads.c',
       'src/threads.h',
       'src/tt.c',
       'src/tt.h',
       'src/uci.c',
       'src/uci.h']
executable('athena', src, dependencies : [math_dep, thread_dep])
