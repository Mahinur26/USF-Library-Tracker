export const FLOOR_NUMS = [1, 2, 3, 4, 5] as const;

/** Design capacity per floor (seats / fire code, etc.). */
export const FLOOR_MAX_CAPACITY: Record<(typeof FLOOR_NUMS)[number], number> = {
  1: 235,
  2: 160,
  3: 210,
  4: 120,
  5: 250,
};

export const BUILDING_MAX_CAPACITY = FLOOR_NUMS.reduce(
  (s, n) => s + FLOOR_MAX_CAPACITY[n],
  0
);

/** Trimmed path, or null = listen at DB root (e.g. Floor_1, Floor_2, …). */
export function getOccupancyPathForRef(): string | null {
  const raw = process.env.NEXT_PUBLIC_OCCUPANCY_PATH?.trim();
  return raw && raw.length > 0 ? raw : null;
}

export function getOccupancyPathLabel(): string {
  return (
    getOccupancyPathForRef() ??
    "database root (e.g. Floor_1, Floor_2 with numeric values)"
  );
}

export type FloorRow = {
  id: string;
  label: string;
  count: number;
};

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
  const byNum = new Map<number, number>();
  for (const row of parsed) {
    const n = floorNumberFromKey(row.id);
    if (n != null) byNum.set(n, row.count);
  }
  return FLOOR_NUMS.map((n) => ({
    id: String(n),
    label: `Floor ${n}`,
    count: byNum.get(n) ?? 0,
  }));
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
