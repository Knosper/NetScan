import { describe, expect, it } from "vitest";
import { parseTagToken, parseTags, serializeTags } from "../../src/ui/tagUtils.js";

describe("tag parsing", () => {
    it("splits name and known color at the last colon", () => {
        expect(parseTagToken(" web:Blue ")).toEqual({ name: "web", color: "blue" });
        expect(parseTagToken("db:v2:green")).toEqual({ name: "db:v2", color: "green" });
    });

    it("drops unknown colors but keeps the name", () => {
        expect(parseTagToken("web:pink")).toEqual({ name: "web", color: "" });
    });

    it("treats a leading colon as part of the name and rejects empty tokens", () => {
        expect(parseTagToken(":red")).toEqual({ name: ":red", color: "" });
        expect(parseTagToken("   ")).toBeNull();
        expect(parseTagToken(" :red")).toEqual({ name: ":red", color: "" });
    });

    it("parses comma separated strings and arrays, skipping blanks", () => {
        expect(parseTags("a:red, ,b")).toEqual([{ name: "a", color: "red" }, { name: "b", color: "" }]);
        expect(parseTags(["a", "b:cyan"])).toEqual([{ name: "a", color: "" }, { name: "b", color: "cyan" }]);
        expect(parseTags(null)).toEqual([]);
    });

    it("round-trips through serializeTags", () => {
        const raw = "web:blue,db,ops:red";
        expect(serializeTags(parseTags(raw))).toBe(raw);
    });
});
