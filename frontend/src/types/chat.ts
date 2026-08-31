export type WsType = 'chat' | 'private' | 'heartbeat' | 'system' | 'online_list' | 'online_users'

export interface WsMessage {
  type: WsType
  from: string
  to: string
  content: string
  timestamp: number
  seq?: number
  users?: string[]
}

export interface ApiResponse<T = unknown> {
  code: number
  msg: string
  data?: T
}

export interface LoginData {
  token: string
  userId: number
  username: string
}

export interface FriendItem {
  username: string
  unread: number
  online?: boolean
}

export interface SearchUserData {
  username: string
  exists: boolean
}