export const FLOOR_NUMS = [1, 2, 3, 4, 5] as const;

/** Fallback capacity per floor when RTDB does not send `max_capacity` (100 each). */
export const FLOOR_MAX_CAPACITY: Record<(typeof FLOOR_NUMS)[number], number> = {
  1: 100,
  2: 100,
  3: 100,
  4: 100,
  5: 100,
};

export const BUILDING_MAX_CAPACITY = FLOOR_NUMS.reduce(
  (s, n) => s + FLOOR_MAX_CAPACITY[n],
  0
);

/**
 * RTDB path to the floors node. Default `floors` matches:
 * `floors/Floor_N/{ current_occupancy, max_capacity, ... }`.
 * Set `NEXT_PUBLIC_OCCUPANCY_PATH=/` or `.` to read flat keys at database root (legacy).
 */
export function getOccupancyPathForRef(): string | null {
  const raw = process.env.NEXT_PUBLIC_OCCUPANCY_PATH?.trim();
  if (raw === "/" || raw === ".") return null;
  if (raw && raw.length > 0) return raw;
  return "floors";
}

export function getOccupancyPathLabel(): string {
  const p = getOccupancyPathForRef();
  if (p == null) return "database root (legacy flat Floor_1, …)";
  return p;
}

export type FloorRow = {
  id: string;
  label: string;
  count: number;
  /** From `floors/Floor_N/max_capacity` when present */
  maxCapacityOverride?: number;
};

export function getFloorMaxCapacity(
  row: FloorRow,
  floorNum: (typeof FLOOR_NUMS)[number]
): number {
  return row.maxCapacityOverride ?? FLOOR_MAX_CAPACITY[floorNum];
}

/** Sum of per-floor max (DB override or fallback) for building %. */
export function sumBuildingMaxCapacity(floors: FloorRow[]): number {
  let s = 0;
  for (const n of FLOOR_NUMS) {
    const row = floors.find((f) => f.id === String(n));
    if (row) s += getFloorMaxCapacity(row, n);
  }
  return s;
}

function isFloorOccupancyKey(key: string): boolean {
  return /^\d+$/.test(key) || /^Floor_\d+$/i.test(key);
}

function formatFloorLabel(key: string): string {
  if (/^\d+$/.test(key)) return `Floor ${key}`;
  return key
    .replace(/[-_]/g, " ")
    .replace(/\b\w/g, (c) => c.toUpperCase());
}

/** Map Firebase keys like `Floor_3` or `3` to floor index 1–5. */
export function floorNumberFromKey(key: string): number | null {
  const m = key.match(/^Floor_(\d+)$/i);
  if (m) {
    const n = parseInt(m[1], 10);
    return n >= 1 && n <= 5 ? n : null;
  }
  if (/^\d+$/.test(key)) {
    const n = parseInt(key, 10);
    return n >= 1 && n <= 5 ? n : null;
  }
  return null;
}

export function parseFloors(
  data: unknown,
  opts: { restrictToFloorKeys: boolean }
): FloorRow[] {
  if (data == null || typeof data !== "object") return [];
  const rows: FloorRow[] = [];
  for (const [key, val] of Object.entries(data as Record<string, unknown>)) {
    if (typeof val === "number" && !Number.isNaN(val)) {
      if (opts.restrictToFloorKeys && !isFloorOccupancyKey(key)) continue;
      rows.push({
        id: key,
        label: formatFloorLabel(key),
        count: Math.max(0, Math.floor(val)),
      });
      continue;
    }
    if (val && typeof val === "object" && "current_occupancy" in val) {
      if (opts.restrictToFloorKeys && !isFloorOccupancyKey(key)) continue;
      const o = val as { current_occupancy?: unknown; max_capacity?: unknown };
      const c = Number(o.current_occupancy);
      if (Number.isNaN(c)) continue;
      const maxRaw = Number(o.max_capacity);
      const maxCap =
        !Number.isNaN(maxRaw) && maxRaw > 0 ? Math.floor(maxRaw) : undefined;
      rows.push({
        id: key,
        label: formatFloorLabel(key),
        count: Math.max(0, Math.floor(c)),
        maxCapacityOverride: maxCap,
      });
      continue;
    }
    if (val && typeof val === "object" && "count" in val) {
      if (opts.restrictToFloorKeys && !isFloorOccupancyKey(key)) continue;
      const o = val as { count?: unknown; name?: unknown };
      const n = Number(o.count);
      if (!Number.isNaN(n)) {
        const label =
          typeof o.name === "string" && o.name.trim()
            ? o.name.trim()
            : formatFloorLabel(key);
        rows.push({ id: key, label, count: Math.max(0, Math.floor(n)) });
      }
    }
  }
  return rows.sort((a, b) => {
    const na = floorNumberFromKey(a.id);
    const nb = floorNumberFromKey(b.id);
    if (na != null && nb != null) return na - nb;
    const tailA = a.id.match(/(\d+)$/)?.[1];
    const tailB = b.id.match(/(\d+)$/)?.[1];
    if (tailA != null && tailB != null) {
      return parseInt(tailA, 10) - parseInt(tailB, 10);
    }
    return a.id.localeCompare(b.id, undefined, { numeric: true });
  });
}

export function mergeToFiveFloors(parsed: FloorRow[]): FloorRow[] {
  const byNum = new Map<number, FloorRow>();
  for (const row of parsed) {
    const n = floorNumberFromKey(row.id);
    if (n != null) byNum.set(n, row);
  }
  return FLOOR_NUMS.map((n) => {
    const existing = byNum.get(n);
    if (existing) {
      return {
        id: String(n),
        label: `Floor ${n}`,
        count: existing.count,
        maxCapacityOverride: existing.maxCapacityOverride,
      };
    }
    return {
      id: String(n),
      label: `Floor ${n}`,
      count: 0,
    };
  });
}

/** Sample rows before merge (ids 1–5). */
export const DEMO_PARSED: FloorRow[] = [
  { id: "1", label: "Floor 1", count: 142 },
  { id: "2", label: "Floor 2", count: 98 },
  { id: "3", label: "Floor 3", count: 76 },
  { id: "4", label: "Floor 4", count: 54 },
  { id: "5", label: "Floor 5", count: 31 },
];

export function buildFloorsFromSnapshot(data: unknown): FloorRow[] {
  const path = getOccupancyPathForRef();
  const restrictToFloorKeys = path == null;
  return mergeToFiveFloors(
    parseFloors(data, { restrictToFloorKeys })
  );
}
