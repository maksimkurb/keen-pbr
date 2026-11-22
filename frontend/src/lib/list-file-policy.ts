/**
 * The rule the daemon applies to a list `file` that is set or changed through
 * the API: an absolute path to a .lst, .txt or .list file inside the directory
 * that holds the config file. This mirrors src/config/list_file_policy.cpp only
 * to tell the user early; the server decides.
 */

export type ListFileProblem =
  | { kind: "notAbsolute" }
  | { kind: "controlCharacter" }
  | { kind: "badExtension" }
  | { kind: "emptyPart" }
  | { kind: "dotPart"; part: string }
  | { kind: "outsideDirectory"; directory: string }

const EXTENSIONS = [".lst", ".txt", ".list"]

// eslint-disable-next-line no-control-regex
const CONTROL_CHARACTER = /[\u0000-\u001f\u007f]/

/** Why the API would refuse `path` as a list file, or null when it would accept it. */
export function getListFileProblem(
  path: string,
  configDir: string
): ListFileProblem | null {
  if (!path.startsWith("/")) return { kind: "notAbsolute" }
  if (CONTROL_CHARACTER.test(path)) return { kind: "controlCharacter" }
  for (const part of path.slice(1).split("/")) {
    if (part === "") return { kind: "emptyPart" }
    if (part === "." || part === "..") return { kind: "dotPart", part }
  }
  if (
    !EXTENSIONS.some((ext) => path.endsWith(ext) && path.length > ext.length)
  ) {
    return { kind: "badExtension" }
  }
  const directory = configDir.replace(/\/+$/, "")
  if (!path.startsWith(`${directory}/`)) {
    return { kind: "outsideDirectory", directory: configDir }
  }
  return null
}
