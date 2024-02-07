import { IEvents, TEvent } from './events';
import { TDisposer } from './types';

export enum TreeEventType {
  NodeInserting = 'node-inserting',
  NodeInserted = 'node-inserted',
  NodeRemoving = 'node-removing',
  NodeRemoved = 'node-removed',
}

export type TTreeEvent = TEvent<TreeEventType>;
export type TTreeNodeEvent = TEvent<TreeEventType> & {
  readonly node: INode;
  readonly parent: INode;
};

export interface TreeEventInterfaces {
  [TreeEventType.NodeInserting]: TTreeNodeEvent;
  [TreeEventType.NodeInserted]: TTreeNodeEvent;
  [TreeEventType.NodeRemoving]: TTreeNodeEvent;
  [TreeEventType.NodeRemoved]: TTreeNodeEvent;
}

export interface ITree {
  readonly root: INode;
  readonly events: IEvents<TreeEventType, TreeEventInterfaces>;
}

export interface ISubtree<T extends INode> {
  readonly node: INode;
  flat(): T[];
  watch(changed: (node: T, added: boolean) => void): TDisposer;
}

export interface INode extends Iterable<INode> {
  readonly tree?: ITree;
  readonly parentNode: INode | undefined;
  readonly firstChild: INode | undefined;
  readonly lastChild: INode | undefined;
  readonly prevSibling: INode | undefined;
  readonly nextSibling: INode | undefined;
  insert(node: INode, before?: INode): void;
  remove(node: INode): boolean;
  traverseNext<T extends INode = INode>(root?: INode): T | undefined;
  subtree<T extends INode = INode>(includeSelf?: boolean): ISubtree<T>;
  isAnscestorOf(child: INode): boolean;
  /**
   * @returns true if this node participates in the hierarchy (i.e. its deepest ancestor is the tree's root)
   * @return false if this node has no parent or
   */
  readonly isAttached: boolean;
  attach(parent: INode): void;
  detach(): void;
}
class Subtree<T extends INode> implements ISubtree<T> {
  constructor(
    readonly node: INode,
    readonly includeRoot: boolean
  ) {}
  watch(changed: (node: T, added: boolean) => void): TDisposer {
    const { tree } = this.node;
    if (!tree) throw new Error('node does not belong to a tree');
    const sub = tree.events.observable.subscribe((ev) => {
      let added = false;
      switch (ev.type) {
        case TreeEventType.NodeInserted:
          added = true;
        case TreeEventType.NodeRemoved:
          if (this.node.isAnscestorOf(ev.node)) changed(ev.node as T, added);
      }
    });
    this.flat().forEach((n) => changed(n, true));
    return () => sub.unsubscribe();
  }
  flat() {
    const result: Array<T> = this.includeRoot ? [this.node as T] : [];
    let n: T | undefined = this.node.firstChild as T;
    while (n) {
      result.push(n);
      n = n.traverseNext<T>(this.node);
    }
    return result;
  }
}

export abstract class AbstractNode implements INode {
  get parentNode() {
    return this._parent;
  }
  get firstChild() {
    return this._head;
  }
  get lastChild() {
    return this._head?._prev;
  }
  get prevSibling(): INode | undefined {
    return this._prev && (!this._parent || this !== this._parent._head)
      ? this._prev
      : undefined;
  }
  get nextSibling(): INode | undefined {
    return this._next && (!this._parent || this._next != this._parent._head)
      ? this._next
      : undefined;
  }
  constructor(readonly tree?: ITree) {}
  insert(node: AbstractNode, before?: AbstractNode | undefined): void {
    if (!node) throw new Error('no node to insert');
    const oldParent = node._parent;
    if (oldParent != null)
      throw new Error('this node is already a tree member');
    if ((node as unknown) === this)
      throw new Error('cannot insert node into itself');
    let p: AbstractNode | undefined = this._parent;
    while (p) {
      if (p === node) throw new Error('insert would cause circular dependency');
      p = p._parent;
    }
    const { tree } = node;
    if (tree && oldParent != this)
      tree.events.fire({
        type: TreeEventType.NodeInserting,
        node,
        parent: this,
      });

    node._parent = this;
    if (!this._head) {
      node._next = node;
      node._prev = node;
      this._head = node;
    } else {
      const next = before || this._head;
      node._next = next;
      node._prev = next._prev;
      next._prev!._next = node;
      next._prev = node;
      if (before != null && before === this._head) this._head = node;
    }
    if (tree && oldParent != node._parent)
      tree.events.fire({
        type: TreeEventType.NodeInserted,
        node,
        parent: this,
      });
  }
  remove(node: AbstractNode): boolean {
    if (!node || node._parent !== this) return false;
    const { tree } = node;
    if (tree)
      tree.events.fire({
        type: TreeEventType.NodeRemoving,
        node,
        parent: this,
      });
    if (node._next === node) delete this._head;
    else {
      node._next!._prev = node._prev;
      node._prev!._next = node._next;
      if (this._head == node) this._head = node._next;
    }
    delete node._parent;
    delete node._prev;
    delete node._next;
    if (tree)
      tree.events.fire({
        type: TreeEventType.NodeRemoved,
        node,
        parent: this,
      });
    return true;
  }
  traverseNext<T extends INode = INode>(root?: INode) {
    if (root && root === this) return undefined;
    let result: T | undefined = (this.firstChild || this.nextSibling) as T;
    // eslint-disable-next-line @typescript-eslint/no-this-alias
    let parent: INode | undefined = this;
    while (!result) {
      parent = parent.parentNode;
      if (!parent || parent === root) break;
      result = parent.nextSibling as T;
    }
    return result;
  }

  get isAttached(): boolean {
    return !!this.parentNode?.isAttached;
  }
  attach(parent: INode) {
    if (this.isAttached) {
      if (parent === this.parentNode) return;
      this.detach();
    }
    parent.insert(this);
  }
  detach() {
    if (this.parentNode) this.parentNode.remove(this);
  }

  subtree<T extends INode = INode>(includeSelf: boolean = false): ISubtree<T> {
    return new Subtree<T>(this, includeSelf);
  }

  isAnscestorOf(child: INode) {
    for (;;) {
      const parent = child.parentNode;
      if (!parent) return false;
      if (parent === this) return true;
      child = parent;
    }
  }

  [Symbol.iterator](): Iterator<INode, any, undefined> {
    let current: INode | undefined = this.firstChild;
    return {
      next(): IteratorResult<INode> {
        if (current) {
          const result = {
            done: false,
            value: current,
          };
          current = current.nextSibling;
          return result;
        } else {
          return {
            done: true,
            value: null,
          };
        }
      },
    };
  }
  private _parent?: AbstractNode;
  private _next?: AbstractNode;
  private _prev?: AbstractNode;
  private _head?: AbstractNode;
}
