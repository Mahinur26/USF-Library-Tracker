"use client";

import { useEffect, useMemo, useState } from "react";
import {
  BUILDING_MAX_CAPACITY,
  FLOOR_MAX_CAPACITY,
  FLOOR_NUMS,
  type FloorRow,
  getOccupancyPathLabel,
  mergeToFiveFloors,
} from "@/lib/occupancy";

const POLL_INTERVAL_MS = 4000;

type OccupancyResponse =
  | {
      ok: true;
      source: "live" | "demo";
      pathLabel: string;
      floors: FloorRow[];
    }
  | { ok: false; error: string };

function capacityPercentRaw(count: number, maxCap: number): number {
  if (maxCap <= 0) return 0;
  return Math.round((count / maxCap) * 100);
}

function capacityStatus(pct: number): string {
  if (pct <= 0) return "Empty";
  if (pct > 100) return "Over capacity";
  if (pct < 40) return "Plenty of space";
  if (pct < 70) return "Moderate";
  if (pct < 90) return "Busy";
  return "Near capacity";
}

function barFillClass(pct: number): string {
  if (pct >= 95) return "bg-gradient-to-r from-amber-600 to-amber-400";
  if (pct >= 80) return "bg-gradient-to-r from-emerald-700 to-[#34d399]";
  return "bg-gradient-to-r from-emerald-600 to-[#34d399]";
}

export function OccupancyDashboard() {
  const [floors, setFloors] = useState<FloorRow[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [usingDemo, setUsingDemo] = useState(false);

  const pathLabel = getOccupancyPathLabel();

  useEffect(() => {
    let cancelled = false;
    let poll: ReturnType<typeof setInterval> | undefined;

    async function fetchOccupancy() {
      try {
        const res = await fetch("/api/occupancy", { cache: "no-store" });
        const data = (await res.json()) as OccupancyResponse;

        if (cancelled) return;

        if (!data.ok) {
          setError(data.error);
          setFloors(mergeToFiveFloors([]));
          setUsingDemo(false);
          setLoading(false);
          return;
        }

        setError(null);
        setFloors(data.floors);
        setUsingDemo(data.source === "demo");
        setLoading(false);

        if (data.source === "live" && !poll && !cancelled) {
          poll = setInterval(fetchOccupancy, POLL_INTERVAL_MS);
        }
      } catch (e) {
        if (cancelled) return;
        setError(e instanceof Error ? e.message : "Network error");
        setFloors(mergeToFiveFloors([]));
        setUsingDemo(false);
        setLoading(false);
      }
    }

    setLoading(true);
    fetchOccupancy();

    return () => {
      cancelled = true;
      if (poll) clearInterval(poll);
    };
  }, []);

  const total = useMemo(
    () => floors.reduce((s, f) => s + f.count, 0),
    [floors]
  );

  const buildingFillPercent = useMemo(() => {
    if (BUILDING_MAX_CAPACITY <= 0) return 0;
    return Math.round((total / BUILDING_MAX_CAPACITY) * 100);
  }, [total]);

  return (
    <div className="w-full max-w-md px-4 py-10 sm:max-w-lg sm:px-6">
      {usingDemo && (
        <p className="mb-6 rounded-2xl border border-amber-500/25 bg-amber-950/35 px-3 py-2.5 text-center text-sm text-amber-100/95">
          Showing sample data. Set{" "}
          <code className="rounded bg-white/10 px-1 font-mono text-xs text-amber-50">
            NEXT_PUBLIC_FIREBASE_DATABASE_URL
          </code>{" "}
          and write counts at{" "}
          <code className="rounded bg-white/10 px-1 font-mono text-xs text-amber-50">
            {pathLabel}
          </code>{" "}
          for live occupancy.
        </p>
      )}

      <header className="mb-8 text-center">
        <p className="text-xs font-semibold uppercase tracking-[0.2em] text-[#34d399]">
          USF Library
        </p>
        <h1 className="mt-2 font-[family-name:var(--font-geist-sans)] text-3xl font-bold tracking-tight text-white sm:text-4xl">
          Occupancy by floor
        </h1>
        <p className="mt-2 text-sm text-zinc-500">
          Live headcount for levels 1–5
        </p>
      </header>

      <div className="rounded-2xl border border-zinc-800 bg-zinc-900/80 p-5 shadow-[0_0_0_1px_rgba(255,255,255,0.03)_inset] backdrop-blur-sm sm:p-6">
        <div className="flex items-end justify-between gap-4 border-b border-zinc-800 pb-5">
          <div className="min-w-0">
            <p className="text-[11px] font-semibold uppercase tracking-[0.15em] text-zinc-500">
              In the library now
            </p>
            {loading && !usingDemo ? (
              <div
                className="mt-3 h-11 w-32 max-w-full animate-pulse rounded-lg bg-zinc-700/80"
                aria-hidden
              />
            ) : (
              <>
                <p className="mt-1 font-[family-name:var(--font-geist-sans)] text-4xl font-semibold tabular-nums tracking-tight text-white sm:text-5xl">
                  {total.toLocaleString()}
                  <span className="text-lg font-medium text-zinc-500 sm:text-xl">
                    {" "}
                    / {BUILDING_MAX_CAPACITY.toLocaleString()}
                  </span>
                </p>
                <p className="mt-1 text-sm tabular-nums text-zinc-400">
                  {buildingFillPercent}% of building capacity
                </p>
              </>
            )}
          </div>
          {!loading && !error && (
            <span className="shrink-0 inline-flex items-center gap-1.5 rounded-full border border-emerald-500/20 bg-emerald-950/60 px-2.5 py-1 text-xs font-medium text-emerald-400">
              <span className="relative flex h-2 w-2">
                <span className="absolute inline-flex h-full w-full animate-ping rounded-full bg-emerald-400 opacity-60" />
                <span className="relative inline-flex h-2 w-2 rounded-full bg-emerald-400" />
              </span>
              {usingDemo ? "Demo" : "Live"}
            </span>
          )}
        </div>

        {error && (
          <p className="mt-4 rounded-xl border border-red-500/20 bg-red-950/40 px-3 py-2 text-sm text-red-200">
            {error}
          </p>
        )}

        <ul className="mt-5 flex flex-col gap-2.5 sm:gap-3" aria-label="Occupancy by floor">
          {loading && !usingDemo
            ? FLOOR_NUMS.map((n) => (
                <li
                  key={`sk-${n}`}
                  className="flex items-center gap-3 rounded-xl border border-zinc-800/80 bg-black/30 px-3 py-3 sm:gap-4 sm:px-4"
                >
                  <div className="flex h-11 w-11 shrink-0 animate-pulse rounded-xl bg-zinc-800" />
                  <div className="min-w-0 flex-1 space-y-2">
                    <div className="h-4 w-24 animate-pulse rounded bg-zinc-800" />
                    <div className="h-1.5 w-full animate-pulse rounded-full bg-zinc-800" />
                  </div>
                  <div className="h-8 w-12 shrink-0 animate-pulse rounded bg-zinc-800" />
                </li>
              ))
            : floors.map((floor) => {
                const num = parseInt(floor.id, 10) as (typeof FLOOR_NUMS)[number];
                const maxCap = FLOOR_MAX_CAPACITY[num];
                const pct = capacityPercentRaw(floor.count, maxCap);
                const status = capacityStatus(pct);
                const barW =
                  maxCap > 0
                    ? Math.min(100, (floor.count / maxCap) * 100)
                    : 0;
                return (
                  <li
                    key={floor.id}
                    className="group rounded-xl border border-zinc-800/90 bg-gradient-to-br from-zinc-950/80 to-black/50 px-3 py-3 transition-colors hover:border-zinc-700/90 sm:px-4 sm:py-3.5"
                  >
                    <div className="flex items-start gap-3 sm:gap-4">
                      <div
                        className="flex h-11 w-11 shrink-0 items-center justify-center rounded-xl border border-zinc-700/80 bg-zinc-900/90 font-[family-name:var(--font-geist-sans)] text-sm font-bold tabular-nums text-emerald-400 shadow-inner shadow-black/20"
                        aria-hidden
                      >
                        {num}
                      </div>
                      <div className="min-w-0 flex-1">
                        <div className="flex flex-wrap items-baseline justify-between gap-x-3 gap-y-1">
                          <p className="font-medium text-zinc-100">{floor.label}</p>
                          <p
                            className={`font-[family-name:var(--font-geist-sans)] text-2xl font-semibold tabular-nums sm:text-[1.65rem] ${
                              pct > 100 ? "text-amber-400" : "text-[#34d399]"
                            }`}
                          >
                            {pct}%
                          </p>
                        </div>
                        <p className="mt-0.5 text-xs text-zinc-500">
                          <span className="tabular-nums text-zinc-400">
                            {floor.count.toLocaleString()} / {maxCap.toLocaleString()}
                          </span>
                          <span className="text-zinc-600"> max</span>
                          <span className="mx-1.5 text-zinc-700">·</span>
                          {status}
                        </p>
                        <div className="mt-2.5 h-2 overflow-hidden rounded-full bg-zinc-800/90 ring-1 ring-white/5">
                          <div
                            className={`h-full rounded-full transition-[width] duration-500 ease-out ${barFillClass(pct)}`}
                            style={{ width: `${barW}%` }}
                          />
                        </div>
                      </div>
                    </div>
                  </li>
                );
              })}
        </ul>
      </div>

      <p className="mt-6 text-center text-xs text-zinc-500">
        Realtime USF library traffic.
      </p>
    </div>
  );
}
