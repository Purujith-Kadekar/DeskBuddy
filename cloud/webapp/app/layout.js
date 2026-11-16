import './globals.css';

export const metadata = {
  title: 'Planner · DeskBuddy & Ventuno Q',
  description: 'Cloud-first tasks, reminders and alarms for the DeskBuddy and Ventuno Q',
};

export default function RootLayout({ children }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
