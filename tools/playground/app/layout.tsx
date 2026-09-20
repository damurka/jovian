import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
    title: 'jovian playground',
    description: 'Real R and Python kernel sessions driven through the jovian public API'
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
    return (
        <html lang="en">
            <body>{children}</body>
        </html>
    );
}
