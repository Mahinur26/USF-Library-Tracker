import { FLOOR_MAX_CAPACITY, FLOOR_NUMS } from "@/lib/occupancy";

export type FloorPeakProfile = {
  /** When this floor is usually busiest */
  busiestDays: string;
  /** Typical peak window (local Tampa time) */
  peakTimeRange: string;
  /** Approximate headcount at peak on busy days */
  typicalPeakCount: number;
  /** Lighter traffic periods */
  quieterDays: string;
};

/**
 * Typical semester patterns (replace with analytics from stored history when available).
 */
export const FLOOR_PEAK_PROFILE: Record<
  (typeof FLOOR_NUMS)[number],
  FloorPeakProfile
> = {
  1: {
    busiestDays: "Tuesday – Thursday",
    peakTimeRange: "2:00 – 6:00 PM",
    typicalPeakCount: 205,
    quieterDays: "Monday mornings · Sunday",
  },
  2: {
    busiestDays: "Monday – Wednesday",
    peakTimeRange: "11:00 AM – 4:00 PM",
    typicalPeakCount: 138,
    quieterDays: "Friday · weekend afternoons",
  },
  3: {
    busiestDays: "Tuesday – Thursday",
    peakTimeRange: "1:00 – 7:00 PM",
    typicalPeakCount: 182,
    quieterDays: "Saturday · early Friday",
  },
  4: {
    busiestDays: "Wednesday – Thursday",
    peakTimeRange: "3:00 – 8:00 PM",
    typicalPeakCount: 95,
    quieterDays: "Weekend · before noon",
  },
  5: {
    busiestDays: "Monday – Thursday",
    peakTimeRange: "5:00 – 10:00 PM",
    typicalPeakCount: 198,
    quieterDays: "Saturday daytime · Sunday morning",
  },
};

/** Tampa campus clock for heuristics */
const TZ = "America/New_York";

function getEtParts(d: Date): { hour: number; day: number } {
  const parts = new Intl.DateTimeFormat("en-US", {
    timeZone: TZ,
    hour: "numeric",
    hour12: false,
    weekday: "short",
  }).formatToParts(d);

  const hour = parseInt(
    parts.find((p) => p.type === "hour")?.value ?? "0",
    10
  );
  const wd = parts.find((p) => p.type === "weekday")?.value ?? "Mon";
  const dayMap: Record<string, number> = {
    Sun: 0,
    Mon: 1,
    Tue: 2,
    Wed: 3,
    Thu: 4,
    Fri: 5,
    Sat: 6,
  };
  return { hour, day: dayMap[wd] ?? 1 };
}

/** Expected change in headcount over the next hour (weekday, rough curve). */
function weekdayDriftForHour(hour: number): number {
  if (hour >= 7 && hour <= 9) return 16;
  if (hour >= 10 && hour <= 11) return 7;
  if (hour >= 12 && hour <= 13) return -10;
  if (hour >= 14 && hour <= 17) return 12;
  if (hour >= 18 && hour <= 20) return 5;
  if (hour >= 21 && hour <= 23) return -18;
  return -6;
}

function weekendDriftForHour(hour: number): number {
  if (hour >= 11 && hour <= 18) return 5;
  if (hour >= 19 && hour <= 22) return -7;
  return -3;
}

export type NextHourPrediction = {
  low: number;
  high: number;
  midpoint: number;
  trend: "up" | "down" | "steady";
};

/**
 * Heuristic next-hour band: time-of-day + day-of-week drift scaled by floor capacity.
 * Not trained on your live history — wire stored metrics here later for real forecasts.
 */
export function predictNextHourRange(
  current: number,
  floorNum: (typeof FLOOR_NUMS)[number],
  now: Date
): NextHourPrediction {
  const maxCap = FLOOR_MAX_CAPACITY[floorNum];
  const { hour, day } = getEtParts(now);
  const weekend = day === 0 || day === 6;
  const base = weekend ? weekendDriftForHour(hour) : weekdayDriftForHour(hour);
  const scale = maxCap / 185;
  const drift = Math.round(base * scale);
  let midpoint = Math.max(0, Math.min(maxCap, current + drift));

  const uncertainty = Math.max(6, Math.round(maxCap * 0.065));
  const low = Math.max(0, midpoint - uncertainty);
  const high = Math.min(maxCap, midpoint + uncertainty);

  const thresh = Math.max(2, Math.round(maxCap * 0.018));
  const trend: NextHourPrediction["trend"] =
    drift > thresh ? "up" : drift < -thresh ? "down" : "steady";

  return { low, high, midpoint, trend };
}

export function formatPredictionTimeLabel(now: Date): string {
  return new Intl.DateTimeFormat("en-US", {
    timeZone: TZ,
    weekday: "short",
    hour: "numeric",
    minute: "2-digit",
  }).format(now);
}
