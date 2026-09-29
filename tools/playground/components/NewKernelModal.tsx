'use client';

import { useEffect, useState } from 'react';
import type { NewSessionRequest } from '@/lib/client/api';
import type { Defaults, Installation, KernelType, StataEdition } from '@/lib/types';

interface Props {
    open: boolean;
    defaults: Defaults | null;
    onClose: () => void;
    /** Resolves when the kernel is up; rejects with the reason it was not. */
    onCreate: (request: NewSessionRequest) => Promise<void>;
}

const OTHER = '__other__';
const EDITION_NAMES: Record<StataEdition, string> = { mp: 'Stata/MP', se: 'Stata/SE', be: 'Stata/BE' };

function describeStata(installation: Installation): string {
    const editions = (installation.editions ?? []).map((e) => e.toUpperCase()).join(', ');
    return `${installation.label} · ${editions} · ${installation.licensed ? 'licensed' : 'no license'}`;
}

/**
 * A choice among the installations found on the server's machine, with
 * "Another location…" for one it did not find (then a path is typed).
 */
function InstallationPicker({ label, installations, defaultHome, value, other, onPick, onOther, onType, describe, what }: {
    label: string;
    installations: Installation[];
    /** The one a session gets when nothing is chosen, marked as such. */
    defaultHome: string;
    value: string;
    other: boolean;
    onPick: (installation: Installation) => void;
    onOther: () => void;
    onType: (home: string) => void;
    describe: (installation: Installation) => string;
    /** "R", "Python", "Stata 17 or newer" -- for the empty and custom cases. */
    what: string;
}) {
    const selected = installations.find((i) => i.home === value);
    return (
        <div className="field">
            <label>{label}</label>
            {installations.length > 0 && (
                <select
                    value={other ? OTHER : value}
                    onChange={(e) => {
                        const picked = installations.find((i) => i.home === e.target.value);
                        if (picked) onPick(picked);
                        else onOther();
                    }}
                >
                    {installations.map((installation) => (
                        <option key={installation.home} value={installation.home}>
                            {describe(installation)}{installation.home === defaultHome ? ' (default)' : ''}
                        </option>
                    ))}
                    <option value={OTHER}>Another location…</option>
                </select>
            )}
            {other || installations.length === 0 ? (
                <>
                    <input
                        value={value}
                        spellCheck={false}
                        placeholder={`Folder of the ${what} installation`}
                        onChange={(e) => onType(e.target.value)}
                    />
                    {installations.length === 0 && <div className="hint">No {what} installation was found on this machine. Enter the folder it is installed in.</div>}
                </>
            ) : (
                selected && <div className="hint path-hint">{selected.home}</div>
            )}
        </div>
    );
}

/** The "Launch New Kernel Session" dialog: pick one of the installations found on this machine, or give a folder. */
export function NewKernelModal({ open, defaults, onClose, onCreate }: Props) {
    const [name, setName] = useState('');
    const [kernelType, setKernelType] = useState<KernelType>('r');
    const [rHome, setRHome] = useState('');
    const [rPath, setRPath] = useState('');
    const [rLibs, setRLibs] = useState('');
    const [pythonHome, setPythonHome] = useState('');
    const [pythonPath, setPythonPath] = useState('');
    const [venvPath, setVenvPath] = useState('');
    const [stataHome, setStataHome] = useState('');
    const [stataEdition, setStataEdition] = useState<StataEdition | ''>('');
    const [other, setOther] = useState<Record<KernelType, boolean>>({ r: false, python: false, stata: false });
    const [workingDirectory, setWorkingDirectory] = useState('');
    const [error, setError] = useState('');
    const [busy, setBusy] = useState(false);

    const installations = defaults?.installations ?? { r: [], python: [], stata: [] };
    const isOther = (kind: KernelType, home: string) =>
        installations[kind].length === 0 || (home !== '' && !installations[kind].some((i) => i.home === home));

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
        setStataHome(defaults?.stataHome ?? '');
        setOther({
            r: isOther('r', defaults?.rHome ?? ''),
            python: isOther('python', defaults?.pythonHome ?? ''),
            stata: isOther('stata', defaults?.stataHome ?? '')
        });
        setWorkingDirectory('');
        setError('');
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [open, defaults]);

    const selectedStata = other.stata ? undefined : installations.stata.find((i) => i.home === stataHome);
    const stataEditions: StataEdition[] = selectedStata?.editions ?? [];

    // A chosen Stata with several editions starts on the first (the one tried
    // first anyway); with one there is nothing to choose.
    useEffect(() => {
        setStataEdition(stataEditions.length > 1 ? stataEditions[0]! : '');
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [stataHome, other.stata]);

    if (!open) return null;

    const submit = async () => {
        setBusy(true);
        setError('');
        try {
            await onCreate(kernelType === 'stata'
                ? { name, kernelType, stataHome, stataEdition: stataEdition || undefined, workingDirectory }
                : kernelType === 'python'
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

    const pickerFor = (kind: KernelType, value: string, setHome: (home: string) => void, extra: {
        label: string; what: string; describe?: (i: Installation) => string; onPick?: (i: Installation) => void;
    }) => (
        <InstallationPicker
            label={extra.label}
            installations={installations[kind]}
            defaultHome={kind === 'r' ? defaults?.rHome ?? '' : kind === 'python' ? defaults?.pythonHome ?? '' : defaults?.stataHome ?? ''}
            value={value}
            other={other[kind]}
            describe={extra.describe ?? ((i) => i.label)}
            what={extra.what}
            onPick={(installation) => {
                setOther((o) => ({ ...o, [kind]: false }));
                setHome(installation.home);
                extra.onPick?.(installation);
            }}
            onOther={() => setOther((o) => ({ ...o, [kind]: true }))}
            onType={setHome}
        />
    );

    const windows = defaults?.platform === 'win32';

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
                            <option value="stata">Stata (Callisto)</option>
                        </select>
                    </div>
                    {kernelType === 'r' ? (
                        <>
                            {pickerFor('r', rHome, setRHome, {
                                label: 'R installation',
                                what: 'R',
                                onPick: (installation) => setRPath(installation.rPath ?? '')
                            })}
                            {other.r && windows && field('R_PATH (optional)', rPath, setRPath, { hint: 'The folder holding R.dll. Empty: <R folder>\\bin\\x64.' })}
                            {field('R_LIBS (optional)', rLibs, setRLibs)}
                        </>
                    ) : kernelType === 'python' ? (
                        <>
                            {pickerFor('python', pythonHome, setPythonHome, { label: 'Python installation', what: 'Python' })}
                            {field('PYTHONPATH (optional)', pythonPath, setPythonPath)}
                            {field('venv path (optional)', venvPath, setVenvPath, { hint: 'A virtual environment whose packages to use, made from the Python above.' })}
                        </>
                    ) : (
                        <>
                            {pickerFor('stata', stataHome, setStataHome, { label: 'Stata installation', what: 'Stata 17 or newer', describe: describeStata })}
                            {selectedStata && !selectedStata.licensed && (
                                <div className="field-warning">
                                    There is no license file (stata.lic) in this folder, so Stata will not start. Put your license
                                    there, or choose a licensed installation.
                                </div>
                            )}
                            {selectedStata && stataEditions.length === 1 && (
                                <div className="field">
                                    <label>Edition</label>
                                    <div className="static-value">{EDITION_NAMES[stataEditions[0]!]} — the only edition installed there</div>
                                </div>
                            )}
                            {selectedStata && stataEditions.length > 1 && (
                                <div className="field">
                                    <label>Edition</label>
                                    <select value={stataEdition} onChange={(e) => setStataEdition(e.target.value as StataEdition)}>
                                        {stataEditions.map((edition) => (
                                            <option key={edition} value={edition}>{EDITION_NAMES[edition]}</option>
                                        ))}
                                    </select>
                                    <div className="hint">This folder has more than one edition; choose the one your license covers.</div>
                                </div>
                            )}
                            {other.stata && (
                                <div className="field">
                                    <label>Edition</label>
                                    <select value={stataEdition} onChange={(e) => setStataEdition(e.target.value as StataEdition | '')}>
                                        <option value="">Whichever is installed (MP, then SE, then BE)</option>
                                        <option value="mp">Stata/MP</option>
                                        <option value="se">Stata/SE</option>
                                        <option value="be">Stata/BE</option>
                                    </select>
                                </div>
                            )}
                        </>
                    )}
                    {field('Working directory (optional)', workingDirectory, setWorkingDirectory, {
                        placeholder: defaults?.homeDirectory ?? '',
                        hint: 'Where the kernel starts (getwd() / os.getcwd() / pwd). Must exist. Empty = the supervisor\'s own directory.'
                    })}
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
