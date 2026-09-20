'use client';

import { canRun, type SessionView } from '@/lib/client/store';
import { formatBytes } from '@/lib/client/text';
import { describeHeartbeat } from '@/lib/client/heartbeat';
import { kernelLabel } from './ConsoleView';

export function statusDotClass(session: Pick<SessionView, 'status' | 'running'>): string {
    if (session.status === 'starting' || (session.status === 'ready' && session.running)) return 'status-busy';
    if (session.status === 'ready') return 'status-running';
    return 'status-stopped';
}

export function statusLabel(status: string): string {
    return status === 'starting' ? 'starting…' : status;
}

export function kernelBadgeClass(kernelType: string): string {
    return kernelType === 'python' ? 'badge-python' : 'badge-r';
}

interface Props {
    sessions: SessionView[];
    activeId: string | null;
    onSelect: (id: string) => void;
    onRestart: () => void;
    onStop: () => void;
    onInterrupt: () => void;
    onRemove: () => void;
}

export function Sidebar({ sessions, activeId, onSelect, onRestart, onStop, onInterrupt, onRemove }: Props) {
    const active = sessions.find((s) => s.id === activeId);
    const runnable = active ? canRun(active.status) : false;
    const heartbeat = describeHeartbeat(active?.heartbeat ?? null, active?.status ?? 'stopped');

    return (
        <aside className="sidebar">
            <div className="sidebar-section-header">
                <span>Sessions ({sessions.length})</span>
            </div>

            <ul className="session-list" id="sessionList">
                {sessions.map((s) => (
                    <li key={s.id} className={'session-item' + (s.id === activeId ? ' active' : '')} onClick={() => onSelect(s.id)}>
                        <div className="session-top">
                            <div className="session-title">
                                <span className={`status-dot ${statusDotClass(s)}`} />
                                <span className="name">{s.name}</span>
                            </div>
                            <span className={`kernel-badge ${kernelBadgeClass(s.kernelType)}`}>{kernelLabel(s.kernelType)}</span>
                        </div>
                        <div className="session-meta">
                            <span>{s.pid ? `PID: ${s.pid}` : s.id.slice(0, 8)}</span>
                            <span>{s.status === 'ready' && s.memoryBytes != null ? formatBytes(s.memoryBytes) : statusLabel(s.status)}</span>
                        </div>
                        {s.workingDirectory && <div className="session-workdir" title={s.workingDirectory}>{s.workingDirectory}</div>}
                    </li>
                ))}
            </ul>

            {active && (
                <div className="kernel-quick-ctrls" id="kernelCtrls">
                    <div className="lifecycle-header">
                        <span>KERNEL LIFECYCLE</span>
                        <span className={`heartbeat hb-${heartbeat.tone}`} title="Round trip of the kernel's heartbeat ping. The kernel answers from its own thread, so this stays live while it runs code.">
                            HEARTBEAT {heartbeat.text}
                        </span>
                    </div>
                    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                        {/* Session.restart() refuses once a session was stopped (create a new one instead). */}
                        <button className="btn-warn" disabled={active.status === 'starting' || active.status === 'stopped'} onClick={onRestart}>
                            <span>&#8635;</span> Restart
                        </button>
                        {/* Stop shuts the kernel down (gracefully, force-killing it if it does not exit) and, for a crashed one, releases it. */}
                        <button className="btn-danger" disabled={active.status === 'stopped'} onClick={onStop}>
                            <span>&#9209;</span> Stop
                        </button>
                    </div>
                    <button className="btn-secondary" style={{ width: '100%', fontSize: 11, padding: 4 }} disabled={!runnable} onClick={onInterrupt}>
                        <span>&#9889;</span> Send SIGINT (Interrupt)
                    </button>
                    <button className="btn-secondary" style={{ width: '100%', fontSize: 11, padding: 4 }} onClick={onRemove}>
                        <span>&#128465;</span> Remove Session
                    </button>
                </div>
            )}
        </aside>
    );
}
