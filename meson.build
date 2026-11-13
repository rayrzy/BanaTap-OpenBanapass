project('BanaTap', 'c', 'cpp', version: '1.0.4',
    default_options: [
        'buildtype=release',
        'optimization=s',
        'strip=true',
        'b_lto=true',
        'cpp_std=c++11',
    ]
)

cpp = meson.get_compiler('cpp')

add_project_arguments(
    cpp.get_supported_arguments(
        '-D_WIN32_WINNT=_WIN32_WINNT_WIN10',
        '-fno-exceptions',
        '-fno-rtti',
        '-ffunction-sections',
        '-fdata-sections',
        '-fno-ident',
    ),
    language: 'cpp',
)

add_project_link_arguments(
    cpp.get_supported_arguments(
        '-s',
        '-static-libgcc',
        '-static-libstdc++',
        '-Wl,--gc-sections',
        '-Wl,--exclude-all-symbols',
    ),
    language: 'cpp',
)

# Version information: project version plus git revision (falls back to
# "unknown" when git or the repository is not available). It ends up in the
# log, and in the DLL's Properties > Details tab when windres is available.
bt_rev = 'unknown'
git_cmd = find_program('git', required: false)
if git_cmd.found()
    git_run = run_command(git_cmd, '-C', meson.project_source_root(), 'describe', '--always', '--dirty', '--tags', check: false)
    if git_run.returncode() == 0
        bt_rev = git_run.stdout().strip()
    endif
endif
bt_version = meson.project_version() + '-' + bt_rev

add_project_arguments('-DBANATAP_VERSION="' + bt_version + '"', language: 'cpp')

windows = import('windows')
windres = find_program('windres', required: false)

bt_ver_parts = meson.project_version().split('.')

# MinHook subproject
minhook_proj = subproject('minhook')
minhook_dep = minhook_proj.get_variable('minhook_dep')

# Shared source files
common_sources = [
    'src/banapass.cpp',
]

# Per-revision version resources (skipped when windres is not found).
# Meson makes a configuration_data read-only once configure_file() has used
# it, so every revision gets its own fresh object instead of reusing one.
rc_revs = {
    'wm5_10500': 'WM5 1.05.00',
    'w5x_20002': 'W5X 2.00.02',
    'w5p_30500': 'W5P 3.05.00',
    'wm6_10304': 'WM6 1.03.04',
    'w6r_20008': 'W6R 2.00.08',
    'w6w_30503': 'W6W 3.05.03',
}
rc_res = {}
foreach rev, game : rc_revs
    rc_out = []
    if windres.found()
        rc_conf = configuration_data()
        rc_conf.set('BT_VERSION', bt_version)
        rc_conf.set('BT_REV', bt_rev)
        rc_conf.set('BT_VER_MAJOR', bt_ver_parts[0])
        rc_conf.set('BT_VER_MINOR', bt_ver_parts[1])
        rc_conf.set('BT_VER_PATCH', bt_ver_parts[2])
        rc_conf.set('BT_GAME', game)
        rc_out = [windows.compile_resources(configure_file(
            input: 'src/version.rc.in',
            output: 'version_' + rev + '.rc',
            configuration: rc_conf,
        ))]
    endif
    rc_res += {rev: rc_out}
endforeach
rc_wm5_10500 = rc_res['wm5_10500']
rc_w5x_20002 = rc_res['w5x_20002']
rc_w5p_30500 = rc_res['w5p_30500']
rc_wm6_10304 = rc_res['wm6_10304']
rc_w6r_20008 = rc_res['w6r_20008']
rc_w6w_30503 = rc_res['w6w_30503']

# WM5 1.05.00
library(
    'bngrw_wm5_10500',
    include_directories: ['src'],
    sources: common_sources + ['src/dllmain_wm5_10500.cpp'] + rc_wm5_10500,
    dependencies: [minhook_dep],
    name_prefix: '',
    link_args: ['-static'],
)

# W5X 2.00.02
library(
    'bngrw_w5x_20002',
    include_directories: ['src'],
    sources: common_sources + ['src/dllmain_w5x_20002.cpp'] + rc_w5x_20002,
    dependencies: [minhook_dep],
    name_prefix: '',
    link_args: ['-static'],
)

# W5P 3.05.00
library(
    'bngrw_w5p_30500',
    include_directories: ['src'],
    sources: common_sources + ['src/dllmain_w5p_30500.cpp'] + rc_w5p_30500,
    dependencies: [minhook_dep],
    name_prefix: '',
    link_args: ['-static'],
)

# WM6 1.03.04
library(
    'bngrw_wm6_10304',
    include_directories: ['src'],
    sources: common_sources + ['src/dllmain_wm6_10304.cpp'] + rc_wm6_10304,
    name_prefix: '',
    link_args: ['-static'],
)

# W6R 2.00.08
library(
    'bngrw_w6r_20008',
    include_directories: ['src'],
    sources: common_sources + ['src/dllmain_w6r_20008.cpp'] + rc_w6r_20008,
    name_prefix: '',
    link_args: ['-static'],
)

# W6W 3.05.03
library(
    'bngrw_w6w_30503',
    include_directories: ['src'],
    sources: common_sources + ['src/dllmain_w6w_30503.cpp'] + rc_w6w_30503,
    name_prefix: '',
    link_args: ['-static'],
)
