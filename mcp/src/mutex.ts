/** Serialises read-modify-write config edits inside this MCP process. */
export class Mutex {
  private tail: Promise<unknown> = Promise.resolve();

  run<T>(body: () => Promise<T>): Promise<T> {
    const result = this.tail.then(body, body);
    this.tail = result.then(
      () => undefined,
      () => undefined,
    );
    return result;
  }
}
