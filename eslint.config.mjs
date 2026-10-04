// ESLint for Jovian's TypeScript (npm run lint:ts): VS Code's own rules for its sources (its eslint.config.js), less
// the ones about VS Code's code base. Layout (indentation, spaces) is the formatter's: npm run format.
import tsParser from '@typescript-eslint/parser';
import tsPlugin from '@typescript-eslint/eslint-plugin';

export default [
    {
        ignores: ['dist/**', 'node_modules/**', 'native/**', 'tools/**', 'vcpkg_installed/**', 'examples/**']
    },
    {
        files: ['lib/**/*.ts', 'test/**/*.ts', 'scripts/**/*.ts'],
        languageOptions: {
            parser: tsParser,
            ecmaVersion: 2022,
            sourceType: 'module'
        },
        plugins: {
            '@typescript-eslint': tsPlugin
        },
        rules: {
            'constructor-super': 'warn',
            'curly': ['warn', 'multi-line'],
            'eqeqeq': 'warn',
            'prefer-const': ['warn', { destructuring: 'all' }],
            'no-buffer-constructor': 'warn',
            'no-caller': 'warn',
            'no-case-declarations': 'warn',
            'no-debugger': 'warn',
            'no-duplicate-case': 'warn',
            'no-duplicate-imports': 'warn',
            'no-eval': 'warn',
            'no-async-promise-executor': 'warn',
            'no-extra-semi': 'warn',
            'no-new-wrappers': 'warn',
            'no-sparse-arrays': 'warn',
            'no-throw-literal': 'warn',
            'no-unsafe-finally': 'warn',
            'no-unused-labels': 'warn',
            'no-misleading-character-class': 'warn',
            'no-var': 'warn',
            'semi': 'warn',
            'quotes': ['warn', 'single', { avoidEscape: true, allowTemplateLiterals: true }],
            '@typescript-eslint/naming-convention': ['warn', { selector: 'class', format: ['PascalCase'] }],
            '@typescript-eslint/no-unused-vars': ['warn', { argsIgnorePattern: '^_', varsIgnorePattern: '^_', caughtErrors: 'none' }]
        }
    }
];
