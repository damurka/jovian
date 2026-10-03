import { test } from 'node:test';
import * as assert from 'node:assert';
import {
    compareVersions,
    discoverPythonHome,
    discoverRHome,
    discoverStataHome,
    findRuntime,
    listPythonInstallations,
    listRInstallations,
    listStataInstallations,
    withAbsolutePaths,
    withDiscoveredRuntime,
    PYTHON_SCRIPT,
    type DiscoveryContext
} from '../../../dist/lib/session/runtimes.js';

// A fake machine. answers maps "command arg arg" to the output that command
// would print (anything not listed fails, like a command that is not
// installed); files lists the paths that exist; dirs maps a directory to its
// entries; texts maps a file to its contents; aliases maps a path to its
// canonical spelling (C:\PROGRA~1 -> C:\Program Files). Nothing touches the
// real system.
function context(
    answers: Record<string, string>,
    overrides: Partial<DiscoveryContext> = {},
    { files = [], dirs = {}, texts = {}, aliases = {} }: {
        files?: string[];
        dirs?: Record<string, string[]>;
        texts?: Record<string, string>;
        aliases?: Record<string, string>;
    } = {}
): DiscoveryContext & { calls: string[] } {
    const calls: string[] = [];
    return {
        env: {},
        platform: 'linux',
        run: (command, args) => {
            const key = [command, ...args].join(' ');
            calls.push(key);
            return answers[key];
        },
        runLines: () => [],
        listDir: (dir) => dirs[dir] ?? [],
        exists: (path) => files.includes(path),
        readFile: (path) => texts[path],
        realpath: (path) => aliases[path] ?? path,
        calls,
        ...overrides
    };
}

test('compareVersions', () => {
    assert.ok(compareVersions('4.10.1', '4.9.3') > 0);
    assert.ok(compareVersions('4.6.0', '4.6') > 0);
    assert.strictEqual(compareVersions('4.6.0', '4.6.0'), 0);
});

test('discoverRHome', async (t) => {
    await t.test('prefers R_HOME and runs nothing', async () => {
        const ctx = context({ 'R RHOME': '/usr/lib/R' }, { env: { R_HOME: '/opt/R' } });
        assert.strictEqual(await discoverRHome(ctx), '/opt/R');
        assert.deepStrictEqual(ctx.calls, []);
    });

    await t.test('asks R itself when R_HOME is not set', async () => {
        const ctx = context({ 'R RHOME': '/usr/lib/R' }, {}, { files: ['/usr/lib/R/library/base'] });
        assert.strictEqual(await discoverRHome(ctx), '/usr/lib/R');
    });

    await t.test('accepts a runner that answers asynchronously', async () => {
        const ctx = context({}, { run: async () => '/usr/lib/R' }, { files: ['/usr/lib/R/library/base'] });
        assert.strictEqual(await discoverRHome(ctx), '/usr/lib/R');
    });

    await t.test('reads the Windows registry when R is not on PATH', async () => {
        const ctx = context({
            'reg query HKLM\\SOFTWARE\\R-core\\R /v InstallPath': '    InstallPath    REG_SZ    C:\\Program Files\\R\\R-4.6.0'
        }, { platform: 'win32' }, { files: ['C:\\Program Files\\R\\R-4.6.0\\library\\base'] });
        assert.strictEqual(await discoverRHome(ctx), 'C:\\Program Files\\R\\R-4.6.0');
    });

    await t.test('skips an answer that is not an R installation (a stale registry entry)', async () => {
        const ctx = context({
            'reg query HKLM\\SOFTWARE\\R-core\\R /v InstallPath': '    InstallPath    REG_SZ    C:\\Program Files\\R\\R-4.1.0'
        }, { platform: 'win32' });
        assert.strictEqual(await discoverRHome(ctx), undefined);
    });

    await t.test('falls back to the newest R under Program Files on Windows', async () => {
        const ctx = context({}, { platform: 'win32', env: { ProgramFiles: 'C:\\Program Files' } }, {
            dirs: { 'C:\\Program Files\\R': ['R-4.9.3', 'R-4.10.1', 'Rtools'] },
            files: ['C:\\Program Files\\R\\R-4.9.3\\library\\base', 'C:\\Program Files\\R\\R-4.10.1\\library\\base']
        });
        assert.strictEqual(await discoverRHome(ctx), 'C:\\Program Files\\R\\R-4.10.1');
    });

    await t.test('falls back to /opt/R/<version> and then /usr/lib/R on Linux', async () => {
        const rig = context({}, {}, {
            dirs: { '/opt/R': ['4.5.2', '4.6.0'] },
            files: ['/opt/R/4.6.0/lib/R/library/base', '/usr/lib/R/library/base']
        });
        assert.strictEqual(await discoverRHome(rig), '/opt/R/4.6.0/lib/R');
        assert.strictEqual(await discoverRHome(context({}, {}, { files: ['/usr/lib/R/library/base'] })), '/usr/lib/R');
    });

    await t.test('falls back to the R framework on macOS', async () => {
        const ctx = context({}, { platform: 'darwin' }, { files: ['/Library/Frameworks/R.framework/Resources/library/base'] });
        assert.strictEqual(await discoverRHome(ctx), '/Library/Frameworks/R.framework/Resources');
    });

    await t.test('lists every R framework version on macOS', async () => {
        const base = '/Library/Frameworks/R.framework';
        const ctx = context({}, { platform: 'darwin' }, {
            dirs: { [`${base}/Versions`]: ['4.3-arm64', '4.5-arm64', 'Current'] },
            files: [`${base}/Resources/library/base`, `${base}/Versions/4.3-arm64/Resources/library/base`, `${base}/Versions/4.5-arm64/Resources/library/base`],
            aliases: { [`${base}/Resources`]: `${base}/Versions/4.5-arm64/Resources` }
        });
        assert.deepStrictEqual((await listRInstallations(ctx)).map((r) => r.home), [`${base}/Resources`, `${base}/Versions/4.3-arm64/Resources`]);
    });

    await t.test('lists each version R recorded in the Windows registry', async () => {
        const ctx = context({}, {
            platform: 'win32',
            runLines: (command, args) => command === 'reg' && args.includes('/s') && args[1] === String.raw`HKLM\SOFTWARE\R-core\R`
                ? [String.raw`HKEY_LOCAL_MACHINE\SOFTWARE\R-core\R\4.4.1`, String.raw`InstallPath    REG_SZ    D:\R\R-4.4.1`, String.raw`HKEY_LOCAL_MACHINE\SOFTWARE\R-core\R\4.5.0`, String.raw`InstallPath    REG_SZ    D:\R\R-4.5.0`]
                : []
        }, { files: [String.raw`D:\R\R-4.4.1\library\base`, String.raw`D:\R\R-4.5.0\library\base`] });
        assert.deepStrictEqual((await listRInstallations(ctx)).map((r) => r.home), [String.raw`D:\R\R-4.4.1`, String.raw`D:\R\R-4.5.0`]);
    });

    await t.test('does not look at the registry off Windows', async () => {
        const ctx = context({});
        assert.strictEqual(await discoverRHome(ctx), undefined);
        assert.ok(!ctx.calls.some((call) => call.startsWith('reg ')));
    });

    await t.test('is undefined when R cannot be found', async () => {
        assert.strictEqual(await discoverRHome(context({}, { platform: 'win32' })), undefined);
    });
});

test('discoverPythonHome', async (t) => {
    const script = PYTHON_SCRIPT;

    await t.test('prefers PYTHONHOME', async () => {
        assert.strictEqual(await discoverPythonHome(context({}, { env: { PYTHONHOME: '/opt/py' } })), '/opt/py');
    });

    await t.test('uses python3, and its base prefix rather than a venv prefix', async () => {
        assert.strictEqual(await discoverPythonHome(context({ [`python3 -c ${script}`]: '/usr' }, {}, { files: ['/usr'] })), '/usr');
    });

    await t.test('falls back to python, then to the py launcher on Windows', async () => {
        assert.strictEqual(
            await discoverPythonHome(context({ [`python -c ${script}`]: '/usr/local' }, {}, { files: ['/usr/local'] })),
            '/usr/local'
        );
        assert.strictEqual(
            await discoverPythonHome(context({ [`py -3 -c ${script}`]: 'C:\\Python312' }, { platform: 'win32' }, { files: ['C:\\Python312'] })),
            'C:\\Python312'
        );
    });

    await t.test('skips an answer that does not exist and tries the next Python', async () => {
        const ctx = context({ [`python3 -c ${script}`]: '/gone', [`python -c ${script}`]: '/usr' }, {}, { files: ['/usr'] });
        assert.strictEqual(await discoverPythonHome(ctx), '/usr');
    });

    await t.test('lists pyenv and conda Pythons that are not on PATH', async () => {
        const ctx = context({
            [`/home/me/.pyenv/versions/3.11.9/bin/python3 -c ${script}`]: '/home/me/.pyenv/versions/3.11.9|3.11.9',
            [`/home/me/miniconda3/bin/python3 -c ${script}`]: '/home/me/miniconda3|3.12.4',
            [`/home/me/miniconda3/envs/analysis/bin/python3 -c ${script}`]: '/home/me/miniconda3/envs/analysis|3.10.14'
        }, { env: { HOME: '/home/me' } }, {
            dirs: { '/home/me/.pyenv/versions': ['3.11.9'], '/home/me/miniconda3/envs': ['analysis'] },
            files: [
                '/home/me/.pyenv/versions/3.11.9/bin/python3', '/home/me/.pyenv/versions/3.11.9',
                '/home/me/miniconda3/bin/python3', '/home/me/miniconda3',
                '/home/me/miniconda3/envs/analysis/bin/python3', '/home/me/miniconda3/envs/analysis'
            ]
        });
        assert.deepStrictEqual((await listPythonInstallations(ctx)).map((p) => p.label), ['Python 3.11.9', 'Python 3.12.4', 'Python 3.10.14']);
    });

    await t.test('is undefined when there is no Python', async () => {
        assert.strictEqual(await discoverPythonHome(context({})), undefined);
    });
});

// A fake machine for discoverStataHome, as context() above plus registry:
// what `reg query` prints (the Uninstall search, then each key's
// InstallLocation).
function stataContext(
    platform: string,
    dirs: Record<string, string[]>,
    files: string[],
    env: Record<string, string> = {},
    registry: { search?: string[]; locations?: Record<string, string> } = {}
) {
    const answers: Record<string, string> = {};
    for (const [key, location] of Object.entries(registry.locations ?? {})) {
        answers[`reg query ${key} /v InstallLocation`] = `    InstallLocation    REG_SZ    ${location}`;
    }
    return context(answers, {
        platform,
        env,
        runLines: (command, args) =>
            command === 'reg' && args[1] === 'HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall' ? registry.search ?? [] : []
    }, { files, dirs });
}

test('discoverStataHome', async (t) => {
    await t.test('prefers STATA_HOME and looks at nothing', async () => {
        const ctx = stataContext('linux', {}, [], { STATA_HOME: '/opt/stata' });
        assert.strictEqual(await discoverStataHome(ctx), '/opt/stata');
    });

    await t.test('picks the newest Stata under Program Files on Windows', async () => {
        const ctx = stataContext(
            'win32',
            { 'C:\\Program Files': ['Stata17', 'Stata18', 'R', 'StataNow19'] },
            ['C:\\Program Files\\Stata17\\se-64.dll', 'C:\\Program Files\\Stata18\\mp-64.dll', 'C:\\Program Files\\StataNow19\\mp-64.dll']
        );
        assert.strictEqual(await discoverStataHome(ctx), 'C:\\Program Files\\StataNow19');
    });

    await t.test('uses %ProgramFiles% when it is set', async () => {
        const ctx = stataContext('win32', { 'D:\\Apps': ['Stata18'] }, ['D:\\Apps\\Stata18\\be-64.dll'], { ProgramFiles: 'D:\\Apps' });
        assert.strictEqual(await discoverStataHome(ctx), 'D:\\Apps\\Stata18');
    });

    await t.test('skips a directory without the Stata shared library (Stata 16 and older)', async () => {
        const ctx = stataContext('win32', { 'C:\\Program Files': ['Stata16', 'Stata18'] }, ['C:\\Program Files\\Stata18\\mp-64.dll']);
        assert.strictEqual(await discoverStataHome(ctx), 'C:\\Program Files\\Stata18');
        assert.strictEqual(await discoverStataHome(stataContext('win32', { 'C:\\Program Files': ['Stata16'] }, [])), undefined);
    });

    await t.test('prefers StataNow at the same version', async () => {
        const ctx = stataContext('linux', { '/usr/local': ['stata19', 'statanow19'] }, ['/usr/local/stata19/libstata-mp.so', '/usr/local/statanow19/libstata-mp.so']);
        assert.strictEqual(await discoverStataHome(ctx), '/usr/local/statanow19');
    });

    await t.test('finds the edition app bundles under /Applications on macOS', async () => {
        const ctx = stataContext('darwin', { '/Applications': ['Safari.app', 'Stata'] }, ['/Applications/Stata/StataSE.app/Contents/MacOS/libstata-se.dylib']);
        assert.strictEqual(await discoverStataHome(ctx), '/Applications/Stata');
    });

    await t.test('finds libstata.so (Stata/BE) on Linux', async () => {
        const ctx = stataContext('linux', { '/usr/local': ['bin', 'stata18'] }, ['/usr/local/stata18/libstata.so']);
        assert.strictEqual(await discoverStataHome(ctx), '/usr/local/stata18');
    });

    await t.test('finds a Stata installed outside Program Files through the installer’s registry entry', async () => {
        const key = 'HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{B444226C}';
        const ctx = stataContext('win32', {}, ['D:\\Tools\\StataNow19\\mp-64.dll'], {}, {
            search: [key, 'DisplayName    REG_SZ    StataNow19', 'InstallLocation    REG_SZ    D:\\Tools\\StataNow19\\'],
            locations: { [key]: 'D:\\Tools\\StataNow19\\' }
        });
        assert.strictEqual(await discoverStataHome(ctx), 'D:\\Tools\\StataNow19');
    });

    await t.test('ignores registry entries that are not Stata itself', async () => {
        const key = 'HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{X}';
        const ctx = stataContext('win32', {}, ['D:\\StataTools\\mp-64.dll'], {}, {
            search: [key, 'DisplayName    REG_SZ    StataTools helper'],
            locations: { [key]: 'D:\\StataTools' }
        });
        assert.strictEqual(await discoverStataHome(ctx), undefined);
    });

    await t.test('counts a registry entry and a Program Files folder for the same Stata once', async () => {
        const key = 'HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{A}';
        const ctx = stataContext('win32', { 'C:\\Program Files': ['Stata18'] }, ['C:\\Program Files\\Stata18\\mp-64.dll'], {}, {
            search: [key, 'DisplayName    REG_SZ    Stata18'],
            locations: { [key]: 'C:\\PROGRAM FILES\\Stata18\\' }
        });
        assert.strictEqual(await discoverStataHome(ctx), 'C:\\Program Files\\Stata18');
    });

    await t.test('prefers a licensed Stata over a newer unlicensed one', async () => {
        const ctx = stataContext(
            'win32',
            { 'C:\\Program Files': ['Stata18', 'StataNow19'] },
            ['C:\\Program Files\\Stata18\\mp-64.dll', 'C:\\Program Files\\Stata18\\stata.lic', 'C:\\Program Files\\StataNow19\\mp-64.dll']
        );
        assert.strictEqual(await discoverStataHome(ctx), 'C:\\Program Files\\Stata18');
    });

    await t.test('looks in /opt and on PATH on Linux', async () => {
        assert.strictEqual(
            await discoverStataHome(stataContext('linux', { '/opt': ['stata19'] }, ['/opt/stata19/libstata-se.so'])),
            '/opt/stata19'
        );
        assert.strictEqual(
            await discoverStataHome(stataContext('linux', {}, ['/home/me/stata18/libstata-mp.so'], { PATH: '/usr/bin:/home/me/stata18/' })),
            '/home/me/stata18'
        );
    });

    await t.test('searches the 64-bit Program Files even from a 32-bit process', async () => {
        const ctx = stataContext(
            'win32',
            { 'C:\\Program Files': ['Stata18'] },
            ['C:\\Program Files\\Stata18\\mp-64.dll'],
            { ProgramFiles: 'C:\\Program Files (x86)', ProgramW6432: 'C:\\Program Files' }
        );
        assert.strictEqual(await discoverStataHome(ctx), 'C:\\Program Files\\Stata18');
    });

    await t.test('is undefined when there is no Stata', async () => {
        assert.strictEqual(await discoverStataHome(stataContext('linux', {}, [])), undefined);
    });
});

test('withDiscoveredRuntime', async (t) => {
    await t.test('fills in stataHome for a Stata session and leaves rHome alone', async () => {
        const ctx = stataContext('linux', { '/usr/local': ['stata19'] }, ['/usr/local/stata19/libstata-mp.so', '/usr/lib/R/library/base']);
        ctx.run = () => '/usr/lib/R';
        assert.deepStrictEqual(await withDiscoveredRuntime({ kernelType: 'stata' }, ctx), { kernelType: 'stata', stataHome: '/usr/local/stata19' });
    });

    await t.test('fills in rHome for an R session (the default kernel)', async () => {
        assert.deepStrictEqual(
            await withDiscoveredRuntime({ workingDirectory: '/p' }, context({ 'R RHOME': '/usr/lib/R' }, {}, { files: ['/usr/lib/R/library/base'] })),
            { workingDirectory: '/p', rHome: '/usr/lib/R' }
        );
    });

    await t.test('fills in pythonHome for a Python session and leaves rHome alone', async () => {
        const found = await withDiscoveredRuntime(
            { kernelType: 'python' },
            context({ [`python3 -c ${PYTHON_SCRIPT}`]: '/usr', 'R RHOME': '/usr/lib/R' }, {}, { files: ['/usr'] })
        );
        assert.deepStrictEqual(found, { kernelType: 'python', pythonHome: '/usr' });
    });

    await t.test('never overrides what the caller passed, and runs nothing', async () => {
        const ctx = context({ 'R RHOME': '/usr/lib/R' });
        const given = { kernelType: 'r' as const, rHome: '/my/R' };
        assert.strictEqual(await withDiscoveredRuntime(given, ctx), given);
        assert.deepStrictEqual(ctx.calls, []);
    });

    await t.test('leaves the options as they are when nothing is found', async () => {
        const given = { kernelType: 'python' as const };
        assert.strictEqual(await withDiscoveredRuntime(given, context({})), given);
    });
});

test('withAbsolutePaths', async (t) => {
    await t.test('resolves every path option against the given directory', () => {
        const resolved = withAbsolutePaths(
            { rHome: 'R', pythonHome: '../py', stataHome: '/opt/stata19', workingDirectory: 'work', pandocPath: 'tools/pandoc', kernelType: 'r' },
            '/home/me/project',
            'linux'
        );
        assert.deepStrictEqual(resolved, {
            rHome: '/home/me/project/R',
            pythonHome: '/home/me/py',
            stataHome: '/opt/stata19',
            workingDirectory: '/home/me/project/work',
            pandocPath: '/home/me/project/tools/pandoc',
            kernelType: 'r'
        });
    });

    await t.test('resolves each entry of a path list', () => {
        assert.strictEqual(withAbsolutePaths({ rLibs: 'lib;D:\\shared' }, 'C:\\proj', 'win32').rLibs, 'C:\\proj\\lib;D:\\shared');
        assert.strictEqual(withAbsolutePaths({ pythonPath: 'src:/usr/share/py' }, '/proj', 'linux').pythonPath, '/proj/src:/usr/share/py');
    });

    await t.test('passes an absolute path on exactly as written', () => {
        assert.strictEqual(withAbsolutePaths({ rHome: 'C:/Program Files/R/R-4.6.0' }, 'D:\\work', 'win32').rHome, 'C:/Program Files/R/R-4.6.0');
        assert.strictEqual(withAbsolutePaths({ rHome: '/opt/R-4.6' }, 'D:\\work', 'win32').rHome, '/opt/R-4.6');
    });

    await t.test('leaves unset and empty options alone', () => {
        assert.deepStrictEqual(withAbsolutePaths({ kernelType: 'python', rHome: '' }, '/proj', 'linux'), { kernelType: 'python', rHome: '' });
    });
});

test('listRInstallations', async (t) => {
    await t.test('lists every R found once, with R’s own version, in the order discovery picks', async () => {
        const ctx = context({ 'R RHOME': 'C:\\PROGRA~1\\R\\R-46~1.0' }, { platform: 'win32', env: { ProgramFiles: 'C:\\Program Files' } }, {
            dirs: { 'C:\\Program Files\\R': ['R-4.5.2', 'R-4.6.0'] },
            files: [
                'C:\\PROGRA~1\\R\\R-46~1.0\\library\\base',
                'C:\\Program Files\\R\\R-4.6.0\\library\\base',
                'C:\\Program Files\\R\\R-4.5.2\\library\\base'
            ],
            texts: {
                'C:\\PROGRA~1\\R\\R-46~1.0\\library\\base\\DESCRIPTION': 'Package: base\nVersion: 4.6.0\n',
                'C:\\Program Files\\R\\R-4.5.2\\library\\base\\DESCRIPTION': 'Package: base\nVersion: 4.5.2\n'
            },
            aliases: { 'C:\\PROGRA~1\\R\\R-46~1.0': 'C:\\Program Files\\R\\R-4.6.0' }
        });
        assert.deepStrictEqual(await listRInstallations(ctx), [
            { home: 'C:\\PROGRA~1\\R\\R-46~1.0', version: '4.6.0', label: 'R 4.6.0', source: 'path', usable: true },
            { home: 'C:\\Program Files\\R\\R-4.5.2', version: '4.5.2', label: 'R 4.5.2', source: 'folder', usable: true }
        ]);
    });

    await t.test('is empty when there is no R', async () => {
        assert.deepStrictEqual(await listRInstallations(context({})), []);
    });
});

test('listPythonInstallations', async (t) => {
    await t.test('lists the Python on PATH first, then the others the py launcher knows, once each', async () => {
        const ctx = context({
            [`python3 -c ${PYTHON_SCRIPT}`]: String.raw`C:\Py312|3.12.10|C:\Py312\python.exe|1`,
            [String.raw`C:\Python314\python.exe -c ` + PYTHON_SCRIPT]: String.raw`C:\Python314|3.14.0|C:\Python314\python.exe|1`,
            [String.raw`C:\Py312\python.exe -c ` + PYTHON_SCRIPT]: String.raw`C:\Py312|3.12.10|C:\Py312\python.exe|1`
        }, {
            platform: 'win32',
            runLines: (command, args) => command === 'py' && args[0] === '-0p'
                ? [String.raw`-V:3.14          C:\Python314\python.exe`, String.raw`-V:3.12[-64] *   C:\Py312\python.exe`]
                : []
        }, { files: [String.raw`C:\Py312`, String.raw`C:\Python314`] });
        assert.deepStrictEqual(await listPythonInstallations(ctx), [
            { home: String.raw`C:\Py312`, version: '3.12.10', label: 'Python 3.12.10', source: 'path', usable: true, executable: String.raw`C:\Py312\python.exe` },
            { home: String.raw`C:\Python314`, version: '3.14.0', label: 'Python 3.14.0', source: 'launcher', usable: true, executable: String.raw`C:\Python314\python.exe` }
        ]);
    });

    await t.test('reads the older launcher format too', async () => {
        const ctx = context({ [String.raw`C:\Python311\python.exe -c ` + PYTHON_SCRIPT]: String.raw`C:\Python311|3.11.9|C:\Python311\python.exe|1` },
            { platform: 'win32', runLines: () => [String.raw` -3.11-64 *    C:\Python311\python.exe`] }, { files: [String.raw`C:\Python311`] });
        assert.deepStrictEqual((await listPythonInstallations(ctx)).map((p) => p.home), [String.raw`C:\Python311`]);
    });

    await t.test('on Windows the py launcher’s default comes first', async () => {
        const ctx = context({
            [`py -3 -c ${PYTHON_SCRIPT}`]: String.raw`C:\Py313|3.13.1|C:\Py313\python.exe|1`,
            [`python -c ${PYTHON_SCRIPT}`]: String.raw`C:\Py311|3.11.9|C:\Py311\python.exe|1`
        }, { platform: 'win32' }, { files: [String.raw`C:\Py313`, String.raw`C:\Py311`] });
        assert.deepStrictEqual((await listPythonInstallations(ctx)).map((p) => [p.home, p.source]), [[String.raw`C:\Py313`, 'launcher'], [String.raw`C:\Py311`, 'path']]);
    });

    await t.test('a Python without its shared library is listed, not usable; the Store’s alias is skipped', async () => {
        const ctx = context({
            [`python3 -c ${PYTHON_SCRIPT}`]: '/usr|3.12.3|/usr/bin/python3|0',
            [`python -c ${PYTHON_SCRIPT}`]: String.raw`/store|3.12.3|C:\Users\me\AppData\Local\Microsoft\WindowsApps\python.exe|1`
        }, {}, { files: ['/usr', '/store'] });
        const found = await listPythonInstallations(ctx);
        assert.deepStrictEqual(found.map((p) => [p.home, p.usable]), [['/usr', false]]);
        assert.match(found[0]!.problem!, /shared library/);
    });
});

test('findRuntime', async (t) => {
    const twoRs = () => context({ 'R RHOME': '/opt/R/4.0.5/lib/R' }, {}, {
        dirs: { '/opt/R': ['4.0.5', '4.6.0'] },
        files: ['/opt/R/4.0.5/lib/R/library/base', '/opt/R/4.6.0/lib/R/library/base'],
        texts: {
            '/opt/R/4.0.5/lib/R/library/base/DESCRIPTION': 'Version: 4.0.5\n',
            '/opt/R/4.6.0/lib/R/library/base/DESCRIPTION': 'Version: 4.6.0\n'
        }
    });

    await t.test('picks the first installation that is new enough, past an older one found first', async () => {
        const found = await findRuntime('r', { minVersion: '4.1.0' }, twoRs());
        assert.deepStrictEqual([found?.home, found?.meetsMinimum, found?.source], ['/opt/R/4.6.0/lib/R', true, 'folder']);
    });

    await t.test('falls back to the first found, saying it is too old, when none is new enough', async () => {
        const found = await findRuntime('r', { minVersion: '5.0.0' }, twoRs());
        assert.deepStrictEqual([found?.home, found?.meetsMinimum], ['/opt/R/4.0.5/lib/R', false]);
    });

    await t.test('keeps the chosen home even when too old, and is undefined when it is not R', async () => {
        const found = await findRuntime('r', { home: '/opt/R/4.0.5/lib/R', minVersion: '4.1.0' }, twoRs());
        assert.deepStrictEqual([found?.home, found?.version, found?.meetsMinimum, found?.source], ['/opt/R/4.0.5/lib/R', '4.0.5', false, 'setting']);
        assert.strictEqual(await findRuntime('r', { home: '/not/R' }, twoRs()), undefined);
    });

    await t.test('a chosen Python folder is asked through its interpreter; a usable one wins over one without its library', async () => {
        const ctx = context({
            [`/opt/py/bin/python3 -c ${PYTHON_SCRIPT}`]: '/opt/py|3.12.1|/opt/py/bin/python3|1',
            [`python3 -c ${PYTHON_SCRIPT}`]: '/usr|3.12.3|/usr/bin/python3|0',
            [`python -c ${PYTHON_SCRIPT}`]: '/usr/local|3.11.2|/usr/local/bin/python|1'
        }, {}, { files: ['/opt/py/bin/python3', '/usr', '/usr/local'] });
        const chosen = await findRuntime('python', { home: '/opt/py' }, ctx);
        assert.deepStrictEqual([chosen?.home, chosen?.executable, chosen?.source], ['/opt/py', '/opt/py/bin/python3', 'setting']);
        assert.strictEqual((await findRuntime('python', { minVersion: '3.10' }, ctx))?.home, '/usr/local');
    });

    await t.test('a Stata without a licence is found, not usable, and says why', async () => {
        const found = await findRuntime('stata', {}, stataContext('linux', { '/usr/local': ['stata18'] }, ['/usr/local/stata18/libstata-mp.so']));
        assert.deepStrictEqual([found?.home, found?.usable], ['/usr/local/stata18', false]);
        assert.match(found!.problem!, /licence/);
    });

    await t.test('is undefined when there is none', async () => {
        assert.strictEqual(await findRuntime('r', {}, context({})), undefined);
    });
});

test('listStataInstallations', async (t) => {
    await t.test('says which editions are installed and whether each is licensed, best first', async () => {
        const ctx = stataContext(
            'win32',
            { 'C:\\Program Files': ['Stata18', 'StataNow19'] },
            [
                'C:\\Program Files\\Stata18\\mp-64.dll',
                'C:\\Program Files\\Stata18\\se-64.dll',
                'C:\\Program Files\\Stata18\\stata.lic',
                'C:\\Program Files\\StataNow19\\mp-64.dll'
            ]
        );
        assert.deepStrictEqual(await listStataInstallations(ctx), [
            { home: 'C:\\Program Files\\Stata18', version: '18', label: 'Stata 18', source: 'folder', usable: true, editions: ['mp', 'se'], licensed: true },
            { home: 'C:\\Program Files\\StataNow19', version: '19', label: 'StataNow 19', source: 'folder', usable: false, problem: 'it has no licence (stata.lic); Stata will not start without one', editions: ['mp'], licensed: false }
        ]);
    });

    await t.test('includes STATA_HOME when it is a Stata directory', async () => {
        const ctx = stataContext('linux', {}, ['/opt/custom/libstata-se.so'], { STATA_HOME: '/opt/custom' });
        assert.deepStrictEqual(await listStataInstallations(ctx), [
            { home: '/opt/custom', label: 'Stata', source: 'env', usable: false, problem: 'it has no licence (stata.lic); Stata will not start without one', editions: ['se'], licensed: false }
        ]);
    });
});
