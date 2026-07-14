import { render, screen } from "@testing-library/react";
import { beforeEach, describe, expect, it } from "vitest";
import { App } from "./App";

describe("Mastering Audio UI", () => {
  beforeEach(() => {
    window.location.hash = "";
  });

  it("shows the standalone stem workflow", () => {
    render(<App />);
    expect(screen.getByText("Stem Mix Suite")).toBeInTheDocument();
    expect(screen.getByText("Drop exported FL Studio stems here")).toBeInTheDocument();
    expect(screen.getByText("Export ML example")).toBeDisabled();
  });

  it("shows analyzer metrics in plugin mode", () => {
    window.location.hash = "#plugin";
    render(<App />);
    expect(screen.getByText("FL Studio track analyzer")).toBeInTheDocument();
    expect(screen.getByLabelText("Signal role")).toBeInTheDocument();
  });
});
