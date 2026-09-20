'use client';

import { useState } from 'react';
import { presetsFor } from '@/lib/client/presets';
import { canRun, type SessionView } from '@/lib/client/store';
import { formatBytes } from '@/lib/client/text';
import { kernelLabel } from './ConsoleView';
import { statusLabel } from './Sidebar';

interface Props {
    session: SessionView | null;
    onRunPreset: (code: string, timeout?: number) => void;
}

/** Right-hand panel: quick presets for the active kernel, and its runtime details. */
export function InspectorPanel({ session, onRunPreset }: Props) {
    const [tab, setTab] = useState<'presets' | 'env'>('presets');
    const runnable = session ? canRun(session.status) : false;

    const configLabel = session?.kernelType === 'python' ? 'PYTHONHOME' : 'R_HOME';
    const configValue = session?.kernelType === 'python' ? session.config.pythonHome : session?.config.rHome;

    return (
        <aside className="inspector-panel">
            <div className="tabs-header">
                <button className={'tab-btn' + (tab === 'presets' ? ' active' : '')} onClick={() => setTab('presets')}>Quick Presets</button>
                <button className={'tab-btn' + (tab === 'env' ? ' active' : '')} onClick={() => setTab('env')}>Kernel Runtime</button>
            </div>

            {tab === 'presets' ? (
                <div className="inspector-content">
                    <div className="card">
                        <div className="card-title"><span>Quick Presets</span></div>
                        <div className="preset-list">
                            {!session && <p style={{ color: 'var(--text-muted)', fontSize: 11.5 }}>Select a session to see presets for its kernel.</p>}
                            {session && presetsFor(session.kernelType).map((preset) => (
                                <button
                                    key={preset.label}
                                    className={'preset-row' + (preset.kind ? ` ${preset.kind}` : '')}
                                    disabled={!runnable}
                                    onClick={() => onRunPreset(preset.code, preset.timeout)}
                                >
                                    {preset.label}
                                </button>
                            ))}
                        </div>
                    </div>
                </div>
            ) : (
                <div className="inspector-content">
                    <div className="card">
                        <div className="card-title">Session Details</div>
                        <div className="kv" id="sessionDetails">
                            {!session ? 'No session selected.' : (
                                <>
                                    <div><b>Session ID:</b> <span className="val">{session.id}</span></div>
                                    <div><b>Kernel Type:</b> <span className="val">{kernelLabel(session.kernelType)}</span></div>
                                    <div><b>Kernel Version:</b> <span className="val">{session.version || 'detecting…'}</span></div>
                                    <div><b>Status:</b> <span className="val">{statusLabel(session.status)}</span></div>
                                    <div><b>PID:</b> <span className="val">{session.pid || 'unknown'}</span></div>
                                    <div><b>Memory (RSS):</b> <span className="val">{formatBytes(session.memoryBytes)}</span></div>
                                    <div><b>Execution Count:</b> <span className="val">{session.execCount}</span></div>
                                    <div><b>Working Directory:</b> <span className="val">{session.workingDirectory || 'supervisor default'}</span></div>
                                    <div><b>{configLabel}:</b> <span className="val">{configValue}</span></div>
                                </>
                            )}
                        </div>
                    </div>
                </div>
            )}
        </aside>
    );
}
