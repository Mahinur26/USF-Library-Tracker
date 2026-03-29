import { FLOOR_NUMS } from "@/lib/occupancy";

/** Descriptive peak patterns only (no simulated numbers). */
export type FloorPeakProfile = {
  busiestDays: string;
  peakTimeRange: string;
  quieterDays: string;
};

export const FLOOR_PEAK_PROFILE: Record<
  (typeof FLOOR_NUMS)[number],
  FloorPeakProfile
> = {
  1: {
    busiestDays: "Tuesday – Thursday",
    peakTimeRange: "2:00 – 6:00 PM",
    quieterDays: "Monday mornings · Sunday",
  },
  2: {
    busiestDays: "Monday – Wednesday",
    peakTimeRange: "11:00 AM – 4:00 PM",
    quieterDays: "Friday · weekend afternoons",
  },
  3: {
    busiestDays: "Tuesday – Thursday",
    peakTimeRange: "1:00 – 7:00 PM",
    quieterDays: "Saturday · early Friday",
  },
  4: {
    busiestDays: "Wednesday – Thursday",
    peakTimeRange: "3:00 – 8:00 PM",
    quieterDays: "Weekend · before noon",
  },
  5: {
    busiestDays: "Monday – Thursday",
    peakTimeRange: "5:00 – 10:00 PM",
    quieterDays: "Saturday daytime · Sunday morning",
  },
};
