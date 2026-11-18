#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import json
import os
import os.path
import subprocess
import sys

from waflib import TaskGen

top = '.'
out = 'build'


@TaskGen.feature('astro_math_errno')
@TaskGen.after_method('process_source')
def add_astro_math_errno_flag(task_gen):
    # Astro code checks finite values and never relies on math errno.
    for task in task_gen.tasks:
        if task.inputs and task.inputs[0].path_from(task_gen.path).startswith('src/c/'):
            task.env.append_value('CFLAGS', ['-fno-math-errno'])


def options(ctx):
    ctx.load('pebble_sdk')


def generate_resources():
    with open('catalogs/catalogs.json', encoding='utf-8') as registry_file:
        catalog_files = json.load(registry_file)['catalogs']
    generators = [
        ('tools/build_catalogs.py',
         ['resources/generated/catalogs.bin', 'resources/generated/catalog_credits.inc',
          'resources/generated/catalog_credits.json', 'CREDITS.md'],
         ['catalogs/catalogs.json'] + [os.path.join('catalogs', name) for name in catalog_files],
         ['--about-header', 'resources/generated/catalog_credits.inc',
          '--credits-json', 'resources/generated/catalog_credits.json',
          '--credits-md', 'CREDITS.md']),
        ('tools/build_ephemeris.py', ['resources/generated/ephemeris.bin'],
         ['ephemeris/minor_planets.csv'] +
         ['ephemeris/meeus/{}.csv'.format(name)
          for name in ('moon_longitude', 'moon_latitude', 'pluto')] +
         ['ephemeris/vsop87d/VSOP87D.{}.gz'.format(name)
          for name in ('mer', 'ven', 'ear', 'mar', 'jup', 'sat', 'ura', 'nep')], []),
    ]
    for script, outputs, inputs, extra_args in generators:
        newest = max(os.path.getmtime(path) for path in [script] + inputs)
        if any(not os.path.exists(path) or os.path.getmtime(path) < newest for path in outputs):
            subprocess.check_call([sys.executable, script, '--output', outputs[0]] + extra_args)

    with open('package.json', encoding='utf-8') as manifest:
        version = json.load(manifest)['version']
    if not isinstance(version, str):
        raise ValueError('package.json version must be a string')
    version_header = 'resources/generated/app_version.inc'
    contents = '#define APP_VERSION {}\n'.format(json.dumps(version))
    current_contents = None
    if os.path.exists(version_header):
        with open(version_header, encoding='utf-8') as existing:
            current_contents = existing.read()
    if current_contents != contents:
        with open(version_header, 'w', encoding='utf-8') as output:
            output.write(contents)


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    generate_resources()
    ctx.load('pebble_sdk')


def build(ctx):
    generate_resources()
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        app = ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf,
                            bin_type='app')
        app.features = app.to_list(app.features) + ['astro_math_errno']

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js',
                                         'package.json']),
                   js_entry_file='src/pkjs/index.js')
