import type { KernelType } from '../types.ts';

export interface Preset {
    label: string;
    code: string;
    timeout?: number;
    kind?: 'kind-error' | 'kind-crash';
}

export const R_PRESETS: Preset[] = [
    { label: 'Hello world', code: `print("Hello from the playground!")` },
    { label: 'Math', code: `sum(1:100)` },
    { label: 'Data table', code: `head(mtcars)` },
    { label: 'Plot (display_data)', code: `plot(1:10, main = "Playground Plot")` },
    { label: 'Streaming loop', code: `for (i in 1:5) {\n  cat("tick", i, "\\n")\n  Sys.sleep(0.4)\n}` },
    { label: 'Interactive input (readline)', code: `name <- readline("What is your name? ")\ncat("Hello,", name, "\\n")` },
    { label: 'Working directory', code: `getwd()` },
    { label: 'Warning', code: `warning("This is a simulated warning")` },
    { label: 'R error (stop)', kind: 'kind-error', code: `stop("Simulated error from the playground")` },
    { label: 'Runtime error', kind: 'kind-error', code: `nonexistent_variable_xyz + 1` },
    { label: 'Timeout (3s cap)', kind: 'kind-error', code: `Sys.sleep(30)`, timeout: 3000 },
    { label: 'Crash kernel (quit — ~60s detection)', kind: 'kind-crash', code: `quit(save = "no")` }
];

export const PYTHON_PRESETS: Preset[] = [
    { label: 'Hello world', code: `print("Hello from the playground!")` },
    { label: 'Math', code: `sum(range(1, 101))` },
    { label: 'Comprehension', code: `{k: k ** 2 for k in range(5)}` },
    { label: 'Streaming loop', code: `import time\nfor i in range(5):\n    print("tick", i)\n    time.sleep(0.4)` },
    { label: 'Interactive input (input)', code: `name = input("What is your name? ")\nprint("Hello,", name)` },
    { label: 'Working directory', code: `import os\nos.getcwd()` },
    { label: 'Warning', code: `import warnings\nwarnings.warn("This is a simulated warning")` },
    { label: 'Python error (raise)', kind: 'kind-error', code: `raise RuntimeError("Simulated error from the playground")` },
    { label: 'Runtime error', kind: 'kind-error', code: `nonexistent_variable_xyz + 1` },
    { label: 'Timeout (3s cap)', kind: 'kind-error', code: `import time\ntime.sleep(30)`, timeout: 3000 },
    { label: 'Crash kernel (os._exit — ~60s detection)', kind: 'kind-crash', code: `import os\nos._exit(1)` }
];

export const STATA_PRESETS: Preset[] = [
    { label: 'Hello world', code: 'display "Hello from the playground!"' },
    { label: 'Load data', code: 'sysuse auto, clear\ndescribe, short' },
    { label: 'Summary', code: 'sysuse auto, clear\nsummarize price mpg weight' },
    { label: 'Regression', code: 'sysuse auto, clear\nregress price mpg weight' },
    { label: 'Graph (display_data)', code: 'sysuse auto, clear\nscatter price mpg' },
    { label: 'Streaming loop', code: 'forvalues i = 1/5 {\n    display "tick `i\'"\n    sleep 400\n}' },
    { label: 'Locals persist across cells', code: 'local greeting "set in an earlier cell"\ndisplay "`greeting\'"' },
    { label: 'Working directory', code: 'pwd' },
    { label: 'Stata error (r(111))', kind: 'kind-error', code: 'sysuse auto, clear\nregress price nosuchvar' },
    { label: 'Unknown command (r(199))', kind: 'kind-error', code: 'displayy 1' },
    { label: 'Timeout (3s cap)', kind: 'kind-error', code: 'sleep 30000', timeout: 3000 }
];

const PRESETS: Record<KernelType, Preset[]> = { r: R_PRESETS, python: PYTHON_PRESETS, stata: STATA_PRESETS };

export const presetsFor = (kernelType: KernelType): Preset[] => PRESETS[kernelType] ?? R_PRESETS;
