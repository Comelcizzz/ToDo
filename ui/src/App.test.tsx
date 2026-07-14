import { render, screen } from "@testing-library/react";
import { beforeEach, describe, expect, it } from "vitest";
import { App } from "./App";

describe("Mastering Audio UI", () => {
  beforeEach(() => {
    window.location.hash = "";
  });

  it("shows the standalone stem workflow", () => {
    const { unmount } = render(<App />);
    expect(screen.getByText("Stem Mix Suite")).toBeInTheDocument();
    expect(screen.getByText("Drop exported FL Studio stems here")).toBeInTheDocument();
    expect(screen.getByText("Export ML example")).toBeDisabled();
    unmount();
  });

  it("shows analyzer metrics in plugin mode", () => {
    window.location.hash = "#plugin";
    const { unmount } = render(<App />);
    expect(screen.getByText("FL Studio track analyzer")).toBeInTheDocument();
    expect(screen.getByLabelText("Signal role")).toBeInTheDocument();
    expect(screen.getAllByText("Sample Peak").length).toBeGreaterThan(0);
    expect(screen.getByText("Momentary")).toBeInTheDocument();
    expect(screen.getByText("Short-term")).toBeInTheDocument();
    expect(screen.getByText("Integrated")).toBeInTheDocument();
    expect(screen.getAllByText("True Peak").length).toBeGreaterThan(0);
    expect(screen.getByText("Analysis frames dropped")).toBeInTheDocument();
    expect(screen.getAllByText("Unavailable").length).toBeGreaterThan(0);
    unmount();
  });
});
