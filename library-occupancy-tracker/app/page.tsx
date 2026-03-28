import { OccupancyDashboard } from "./components/occupancy-dashboard";

export default function Home() {
  return (
    <div className="flex min-h-full flex-1 flex-col items-center justify-center bg-black">
      <OccupancyDashboard />
    </div>
  );
}
