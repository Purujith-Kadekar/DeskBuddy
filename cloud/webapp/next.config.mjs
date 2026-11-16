/** @type {import('next').NextConfig} */
const nextConfig = {
  reactStrictMode: true,
  // API routes talk to Firestore with long-lived keep-alive connections;
  // nothing else to configure for this small app.
};

export default nextConfig;
