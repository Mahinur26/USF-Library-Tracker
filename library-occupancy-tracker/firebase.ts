import { initializeApp, getApps } from "firebase/app";
import { getDatabase, type Database } from "firebase/database";

const firebaseConfig = {
  apiKey: process.env.NEXT_PUBLIC_FIREBASE_API_KEY,
  authDomain: process.env.NEXT_PUBLIC_FIREBASE_AUTH_DOMAIN,
  databaseURL: process.env.NEXT_PUBLIC_FIREBASE_DATABASE_URL,
  projectId: process.env.NEXT_PUBLIC_FIREBASE_PROJECT_ID,
};

export function getDb(): Database | null {
  if (!process.env.NEXT_PUBLIC_FIREBASE_DATABASE_URL) {
    return null;
  }
  const app = getApps().length ? getApps()[0]! : initializeApp(firebaseConfig);
  return getDatabase(app);
}
