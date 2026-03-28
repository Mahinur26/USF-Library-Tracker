import { NextResponse } from "next/server";
import { ref, get } from "firebase/database";
import { getDb } from "@/firebase";
import {
  buildFloorsFromSnapshot,
  DEMO_PARSED,
  getOccupancyPathLabel,
  getOccupancyPathForRef,
  mergeToFiveFloors,
} from "@/lib/occupancy";

export const dynamic = "force-dynamic";

export async function GET() {
  const db = getDb();
  if (!db) {
    return NextResponse.json(
      {
        ok: true as const,
        source: "demo" as const,
        pathLabel: getOccupancyPathLabel(),
        floors: mergeToFiveFloors(DEMO_PARSED),
      },
      { headers: { "Cache-Control": "no-store" } }
    );
  }

  try {
    const path = getOccupancyPathForRef();
    const r = path ? ref(db, path) : ref(db);
    const snap = await get(r);
    const floors = buildFloorsFromSnapshot(snap.val());
    return NextResponse.json(
      {
        ok: true as const,
        source: "live" as const,
        pathLabel: getOccupancyPathLabel(),
        floors,
      },
      { headers: { "Cache-Control": "no-store" } }
    );
  } catch (e) {
    const message = e instanceof Error ? e.message : "Unknown error";
    return NextResponse.json(
      { ok: false as const, error: message },
      { status: 502, headers: { "Cache-Control": "no-store" } }
    );
  }
}
