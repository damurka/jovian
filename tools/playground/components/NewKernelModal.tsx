'use client';

import { useEffect, useState } from 'react';
import type { NewSessionRequest } from '@/lib/client/api';
import type { Defaults, KernelType } from '@/lib/types';

interface Props {
    open: boolean;
    defaults: Defaults | null;
    onClose: () => void;
    /** Resolves when the kernel is up; rejects with the reason it was not. */
    onCreate: (request: NewSessionRequest) => Promise<void>;
}

/** The "Launch New Kernel Session" dialog, pre-filled from this machine's detected installs. */
export function NewKernelModal({ open, defaults, onClose, onCreate }: Props) {
    const [name, setName] = useState('');
    const [kernelType, setKernelType] = useState<KernelType>('r');
    const [rHome, setRHome] = useState('');
    const [rPath, setRPath] = useState('');
    const [rLibs, setRLibs] = useState('');
    const [pythonHome, setPythonHome] = useState('');
    const [pythonPath, setPythonPath] = useState('');
    const [venvPath, setVenvPath] = useState('');
    const [workingDirectory, setWorkingDirectory] = useState('');
    const [error, setError] = useState('');
    const [busy, setBusy] = useState(false);

    // Re-seed from the detected defaults each time the dialog opens.
    useEffect(() => {
        if (!open) return;
        setName('');
        setKernelType('r');
        setRHome(defaults?.rHome ?? '');
        setRPath(defaults?.rPath ?? '');
        setRLibs(defaults?.rLibs ?? '');
        setPythonHome(defaults?.pythonHome ?? '');
        setPythonPath('');
        setVenvPath('');
        setWorkingDirectory('');
        setError('');
    }, [open, defaults]);

    if (!open) return null;

    const submit = async () => {
        setBusy(true);
        setError('');
        try {
            await onCreate(kernelType === 'python'
                ? { name, kernelType, pythonHome, pythonPath, venvPath, workingDirectory }
                : { name, kernelType, rHome, rPath, rLibs, workingDirectory });
        } catch (e) {
            setError(String((e as Error).message ?? e));
        } finally {
            setBusy(false);
        }
    };

    const field = (label: string, value: string, set: (v: string) => void, extra?: { placeholder?: string; hint?: string }) => (
        <div className="field">
            <label>{label}</label>
            <input value={value} spellCheck={false} placeholder={extra?.placeholder} onChange={(e) => set(e.target.value)} />
            {extra?.hint && <div className="hint">{extra.hint}</div>}
        </div>
    );

    return (
        <div className="modal-backdrop show" id="newKernelModal">
            <div className="modal">
                <div className="modal-header">
                    <span>Launch New Kernel Session</span>
                    <button onClick={onClose} aria-label="Close">&#10005;</button>
                </div>
                <div className="modal-body">
                    {field('Session Name', name, setName, { placeholder: 'e.g. Data exploration' })}
                    <div className="field">
                        <label>Kernel Engine Type</label>
                        <select id="newKernelType" value={kernelType} onChange={(e) => setKernelType(e.target.value as KernelType)}>
                            <option value="r">R (Elara)</option>
                            <option value="python">Python (Carpo)</option>
                        </select>
                    </div>
                    {kernelType === 'r' ? (
                        <>
                            {field('R_HOME', rHome, setRHome)}
                            {field('R_PATH', rPath, setRPath, { hint: 'Windows only: the folder holding R.dll. Leave empty elsewhere.' })}
                            {field('R_LIBS (optional)', rLibs, setRLibs)}
                        </>
                    ) : (
                        <>
                            {field('PYTHONHOME', pythonHome, setPythonHome)}
                            {field('PYTHONPATH (optional)', pythonPath, setPythonPath)}
                            {field('venv path (optional)', venvPath, setVenvPath)}
                        </>
                    )}
                    {field('Working directory (optional)', workingDirectory, setWorkingDirectory, {
                        placeholder: defaults?.homeDirectory ?? '',
                        hint: 'Where the kernel starts (getwd() / os.getcwd()). Must exist. Empty = the supervisor\'s own directory.'
                    })}
                    <p style={{ color: 'var(--text-muted)', fontSize: 11, lineHeight: 1.5 }}>
                        Pre-filled with this machine&apos;s detected install -- edit to point at a different R/Python
                        installation (e.g. to pick a specific version) before spawning.
                    </p>
                    {error && <div className="modal-error show">{error}</div>}
                </div>
                <div className="modal-footer">
                    <button className="btn-secondary" onClick={onClose}>Cancel</button>
                    <button className="btn-primary" disabled={busy} onClick={() => void submit()}>
                        {busy ? 'Starting…' : 'Spawn Kernel Process'}
                    </button>
                </div>
            </div>
        </div>
    );
}
