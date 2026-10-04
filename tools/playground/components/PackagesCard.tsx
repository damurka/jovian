'use client';

import { useCallback, useEffect, useState } from 'react';
import { api } from '@/lib/client/api';
import { canRun, type SessionView } from '@/lib/client/store';
import type { PackagesInfo, WhenInUse } from '@/lib/types';

interface Props {
    session: SessionView | null;
    /** The progress lines of this session's installs, newest last (the 'packages' stream events). */
    log: readonly string[];
    onLog: (line: string) => void;
    onClearLog: () => void;
}

const WHEN_IN_USE: { value: WhenInUse; label: string; hint: string }[] = [
    { value: 'wait', label: 'Wait for them to end', hint: 'The install waits until no session has the package loaded.' },
    { value: 'defer', label: 'Don\'t install now', hint: 'Fails at once and names the sessions; nothing is installed.' },
    { value: 'proceed', label: 'Install anyway', hint: 'On Windows a loaded package\'s DLL can\'t be replaced, so this can fail.' }
];

/**
 * Installs a package for the active session through the manager's installer
 * (manager.ensureRPackage() / ensurePythonPackages()), which knows which
 * sessions use the library: a package another session has loaded is not
 * replaced under it.
 */
export function PackagesCard({ session, log, onLog, onClearLog }: Props) {
    const [info, setInfo] = useState<PackagesInfo | null>(null);
    const [name, setName] = useState('');
    const [update, setUpdate] = useState(false);
    const [whenInUse, setWhenInUse] = useState<WhenInUse>('wait');
    const [installing, setInstalling] = useState(false);

    const id = session?.id ?? null;
    const status = session?.status;
    const running = session?.running;
    const refresh = useCallback(() => {
        if (!id) {
            setInfo(null);
            return;
        }
        api.packages(id).then(setInfo).catch(() => setInfo(null));
    }, [id]);
    // again when a cell ends (it may have loaded packages) and when the session's state changes
    useEffect(refresh, [refresh, status, running]);

    if (!session) {
        return <div className="card"><div className="card-title">Packages</div><p className="hint-text">Select a session.</p></div>;
    }

    const install = async () => {
        if (!name.trim() || installing) return;
        setInstalling(true);
        onLog(`> ${update ? 'update' : 'install'} ${name.trim()} (${whenInUse})`);
        try {
            const result = await api.installPackage(session.id, name.trim(), update, whenInUse);
            const took = result.ms !== undefined ? ` (${(result.ms / 1000).toFixed(1)} s)` : '';
            if (result.ok) onLog(`${result.summary}${took}`);
            else onLog(`Not installed: ${result.error}${result.inUseBy?.length ? ` [in use by: ${result.inUseBy.join(', ')}]` : ''}${took}`);
        } catch (error) {
            onLog(`Request failed: ${(error as Error).message}`);
        } finally {
            setInstalling(false);
            refresh();
        }
    };

    const isR = session.kernelType === 'r';
    return (
        <>
            <div className="card">
                <div className="card-title"><span>Install a package</span></div>
                {!info ? <p className="hint-text">Loading…</p> : !info.supported ? <p className="hint-text">{info.reason}</p> : (
                    <>
                        <div className="field">
                            <label>{isR ? 'R package' : 'pip requirement'}</label>
                            <input
                                id="packageName" value={name} spellCheck={false} placeholder={isR ? 'glue' : 'six  or  six>=1.17'}
                                onChange={(e) => setName(e.target.value)}
                                onKeyDown={(e) => { if (e.key === 'Enter') void install(); }}
                            />
                            <div className="hint path-hint">into {info.library}</div>
                        </div>
                        <div className="field">
                            <label className="check-label">
                                <input type="checkbox" checked={update} onChange={(e) => setUpdate(e.target.checked)} /> Update it if it is already installed
                            </label>
                        </div>
                        <div className="field">
                            <label>If a session has it loaded</label>
                            <select id="packageWhenInUse" value={whenInUse} onChange={(e) => setWhenInUse(e.target.value as WhenInUse)}>
                                {WHEN_IN_USE.map((option) => <option key={option.value} value={option.value}>{option.label}</option>)}
                            </select>
                            <div className="hint">{WHEN_IN_USE.find((option) => option.value === whenInUse)?.hint}</div>
                        </div>
                        {!info.coordinated && isR && (
                            <div className="field-warning">
                                This session uses R&apos;s own library, so the installer can&apos;t tell which sessions use it and never waits.
                                Give sessions the same first library path (New Kernel Session) to see waiting and deferring.
                            </div>
                        )}
                        <button className="btn-secondary" id="btnInstallPackage" disabled={installing || !name.trim()} onClick={() => void install()}>
                            {installing ? 'Installing…' : update ? 'Install or update' : 'Install'}
                        </button>
                    </>
                )}
            </div>

            {(log.length > 0 || installing) && (
                <div className="card">
                    <div className="card-title">
                        <span>Install log</span>
                        <button className="link-btn" onClick={onClearLog}>clear</button>
                    </div>
                    <pre className="package-log" id="packageLog">{log.join('\n')}</pre>
                </div>
            )}

            {isR && (
                <div className="card">
                    <div className="card-title">
                        <span>Loaded in this session</span>
                        <button className="link-btn" disabled={!canRun(session.status)} onClick={refresh}>refresh</button>
                    </div>
                    <div className="kv" id="loadedPackages">
                        {info?.loaded
                            ? <span className="val">{info.loaded.join(', ') || 'none'}</span>
                            : session.running
                                ? 'Not known while a cell runs: it may load anything, so the session counts as using every package.'
                                : canRun(session.status) ? 'Not known.' : 'The kernel is not running.'}
                    </div>
                </div>
            )}
        </>
    );
}
